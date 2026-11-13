"""One page: HTML (or a WordPress content fragment) to Markdown, with its images and files saved on the way."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import PurePosixPath
from urllib.parse import unquote, urljoin, urlparse

from bs4 import BeautifulSoup, Tag

from .assets import AssetStore
from .options import Options
from .render import Renderer
from . import wix
from .urls import IMAGE_EXT, classify_link

# Page chrome removed before extraction. header/footer/aside inside <article>, <main> or <section> are kept: they
# usually hold the article title or an author box rather than site chrome.
CHROME_SELECTORS = [
    "header", "footer", "nav", "aside",
    "[role=banner]", "[role=contentinfo]", "[role=navigation]", "[role=complementary]", "[role=search]",
    "#cookie-notice", ".cookie-notice", ".cookie-banner", "#cookiebanner", "#onetrust-consent-sdk", ".skip-link",
]
CHROME_PROTECTED_TAGS = {"header", "footer", "aside"}
IGNORE_IMAGES = [
    r"favicon", r"gravatar\.com", r"s\.w\.org/", r"pixel\.wp\.com", r"/wp-content/plugins/", r"/wp-includes/",
    r"(doubleclick|googletagmanager|google-analytics|facebook\.com/tr)",
]
WP_SIZE_SUFFIX = re.compile(r"-\d+x\d+(?=\.\w+$)")
WP_STRIP = ["script", "style", ".sharedaddy", ".jp-relatedposts", ".addthis_toolbox"]


@dataclass
class Extracted:
    title: str = ""
    description: str = ""
    lang: str | None = None
    body: str = ""
    headings: list[dict] = field(default_factory=list)
    word_count: int = 0
    discovered: list[tuple[str, str]] = field(default_factory=list)  # (kind, url) of every link, for the crawl queue
    external: list[str] = field(default_factory=list)
    internal: list[str] = field(default_factory=list)
    images: list[str] = field(default_factory=list)  # asset ids
    files: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)


def ignore_matcher(extra: list[str] = ()):
    patterns = [re.compile(p, re.I) for p in [*IGNORE_IMAGES, *extra]]
    return lambda url: any(p.search(url) for p in patterns)


class PageExtractor:
    def __init__(self, opts: Options, assets: AssetStore, base: str) -> None:
        self.opts = opts
        self.assets = assets
        self.base = base

    def extract(self, html: bytes | str, url: str, page_id: str, rel_prefix: str, *, fragment: bool = False) -> Extracted:
        from .urls import base_host, host_allowed

        soup = BeautifulSoup(html, "lxml")
        out = Extracted()
        if not fragment:
            out.title = soup.title.get_text(" ", strip=True) if soup.title else ""
            for attrs in ({"name": "description"}, {"property": "og:description"}):
                meta = soup.find("meta", attrs=attrs)
                if meta and (meta.get("content") or "").strip():
                    out.description = re.sub(r"\s+", " ", meta["content"]).strip()[:500]
                    break
            lang = ((soup.html.get("lang") if soup.html else "") or "").strip().lower()
            out.lang = re.split(r"[-_]", lang)[0] or None

        seen: set[str] = set()
        for anchor in soup.find_all("a", href=True):
            link = classify_link(anchor["href"], url)
            if not link or link[1] in seen:
                continue
            seen.add(link[1])
            out.discovered.append(link)
            if link[0] == "page":
                (out.internal if host_allowed(link[1], self.base, self.opts.include_subdomains) else out.external).append(link[1])

        wix_page = wix.is_wix(soup)
        wix_scripts = wix.parse_scripts(soup) if wix_page else []  # the scripts are removed next
        for tag in soup(["script", "style", "noscript", "template"]):
            tag.decompose()

        root = self._root(soup, out, fragment)
        if not fragment:
            self._remove_chrome(soup, root)
        if wix_page:
            for gallery in wix.galleries(soup, root, wix_scripts):
                if gallery.total > len(gallery.items):
                    # The page ships only the first items; Wix loads the rest while it is scrolled.
                    fetcher = getattr(self.assets, "fetcher", None)
                    parts = urlparse(url)
                    everything = wix.fetch_all_items(fetcher, f"{parts.scheme}://{parts.netloc}", gallery) if fetcher else None
                    if everything:
                        gallery.items = everything
                    if len(gallery.items) < min(gallery.total, wix.MAX_ITEMS):
                        out.notes.append(
                            f"Wix gallery {gallery.component_id}: {len(gallery.items)} of {gallery.total} items found; "
                            "the rest could not be loaded")
                wix.add_missing_gallery_images(soup, root, gallery)
        for selector in self.opts.strip:
            for element in soup.select(selector):
                if not getattr(element, "decomposed", False) and element is not root and not any(p is element for p in root.parents):
                    element.decompose()
        if fragment:
            for selector in WP_STRIP:
                for element in soup.select(selector):
                    if not getattr(element, "decomposed", False):
                        element.decompose()

        for heading in root.find_all(["h1", "h2", "h3", "h4", "h5", "h6"]):
            text = heading.get_text(" ", strip=True)
            if text:
                out.headings.append({"level": int(heading.name[1]), "text": text[:160]})

        image_ids: list[str] = []
        renderer: Renderer

        def save_image(tag: Tag):
            src = renderer.img_url(tag)
            if not src:
                return None
            candidates = [src]
            anchor = tag.find_parent("a", href=True)
            if anchor:  # lightbox thumbnails usually link to the full-size file
                target = urljoin(url, anchor["href"].strip())
                if PurePosixPath(unquote(urlparse(target).path)).suffix.lower() in IMAGE_EXT:
                    candidates.insert(0, target)
            if "/wp-content/uploads/" in src and WP_SIZE_SUFFIX.search(src):
                candidates.insert(0, WP_SIZE_SUFFIX.sub("", src))  # the full-size original, the resized one as fallback
            # Wix previews: the uploaded file is at the URL without the /v1/... resize part.
            candidates = [o for c in candidates if (o := wix.original_url(c))] + candidates
            alt = (tag.get("alt") or tag.get("title") or "").strip()
            asset = self.assets.save(list(dict.fromkeys(candidates)), "image", page_id, alt)
            if not asset:
                return None
            if asset.id not in image_ids:
                image_ids.append(asset.id)
            return rel_prefix + asset.file, alt

        renderer = Renderer(url, save_image, self.assets.ignore, aria_hidden_is_hidden=not wix_page)
        blocks = renderer.render(root)
        if not blocks:
            out.notes.append("no text found (the page may be rendered by JavaScript)")
        title = out.title or url
        if not any(b.startswith("# ") for b in blocks):
            blocks.insert(0, f"# {title}")

        file_ids: list[str] = []
        listed: list[str] = []
        for anchor in root.find_all("a", href=True):
            link = classify_link(anchor["href"], url)
            if not link or link[0] != "file" or not host_allowed(link[1], self.base, self.opts.include_subdomains):
                continue
            if PurePosixPath(unquote(urlparse(link[1]).path)).suffix.lower() in IMAGE_EXT:
                continue
            text = anchor.get_text(" ", strip=True)
            asset = self.assets.save([link[1]], "file", page_id, text)
            if asset and asset.id not in file_ids:
                file_ids.append(asset.id)
                listed.append(f"- [{text or PurePosixPath(asset.file).name}]({rel_prefix}{asset.file.replace(' ', '%20')})")
        parts = ["\n\n".join(blocks)]
        if listed:
            parts.append("## Linked documents\n\n" + "\n".join(listed))
        out.body = "\n\n".join(parts)
        out.images, out.files = image_ids, file_ids
        out.word_count = len(re.findall(r"\w+", re.sub(r"!?\[[^\]]*\]\([^)]*\)", " ", out.body)))
        return out

    def _root(self, soup: BeautifulSoup, out: Extracted, fragment: bool) -> Tag:
        if fragment:
            return soup.body or soup
        if self.opts.main_selector:
            root = soup.select_one(self.opts.main_selector)
            if root is not None:
                return root
            out.notes.append(f"mainSelector {self.opts.main_selector!r} matched nothing; used the whole page")
        return soup.select_one("main, [role=main]") or soup.body or soup

    def _remove_chrome(self, soup: BeautifulSoup, root: Tag) -> None:
        if self.opts.keep_chrome:
            return

        def encloses_root(element: Tag) -> bool:
            return element is root or any(parent is element for parent in root.parents)

        for selector in CHROME_SELECTORS:
            for element in soup.select(selector):
                if getattr(element, "decomposed", False) or encloses_root(element):
                    continue
                if element.name in CHROME_PROTECTED_TAGS and element.find_parent(["article", "main", "section"]):
                    continue
                element.decompose()
