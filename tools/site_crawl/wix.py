"""Wix sites: full-size images and the gallery items that are not in the page markup.

Wix serves every image through a resizing URL
    https://static.wixstatic.com/media/<file>/v1/fill/w_514,h_386,.../<name>
and the markup only references those previews. The URL without the /v1/... part is the file
as it was uploaded, so the crawler tries that first and keeps the preview as a fallback.

A Wix page is also rendered from JSON that ships inside the HTML. Galleries (Pro Gallery) put
only the items in view into the markup and mark the rest aria-hidden; the item list itself,
with the original file and its size, is in `wix-warmup-data`. This module reads those lists,
scoped to the gallery components that are really inside the content area of the page.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from urllib.parse import urlparse

from bs4 import BeautifulSoup, Tag

MEDIA_HOSTS = {"static.wixstatic.com"}
MEDIA_PATH = re.compile(r"^/media/([^/]+)(?:/v1/.*)?$")
COMPONENT_DATA = re.compile(r"^(comp-[A-Za-z0-9_-]+?)_galleryData$")
IMAGE_SUFFIX = re.compile(r"\.(jpe?g|png|webp|gif|avif)$", re.I)


@dataclass
class GalleryItem:
    url: str  # the uploaded file
    alt: str
    width: int
    height: int
    is_video_poster: bool = False
    source_key: str = ""  # the uploaded file's id without size or extension; a video's posters start with it


@dataclass
class Gallery:
    component_id: str
    items: list[GalleryItem]
    total: int  # how many items the gallery has; usually more than the page ships
    gallery_id: str = ""  # the id the Pro Gallery service knows it by (for paging)


def media_id(url: str) -> str | None:
    """The file part of a Wix media URL (`248d21_ab12~mv2.jpg`), whatever size it is requested in."""
    parts = urlparse(url)
    if parts.netloc.lower() not in MEDIA_HOSTS:
        return None
    match = MEDIA_PATH.match(parts.path)
    return match.group(1) if match else None


def original_url(url: str) -> str | None:
    """The uploaded file behind a Wix resizing URL; None for other URLs or when it already is the file."""
    file = media_id(url)
    if not file:
        return None
    original = f"https://static.wixstatic.com/media/{file}"
    return original if urlparse(url).path != f"/media/{file}" or urlparse(url).query else None


def is_wix(soup: BeautifulSoup) -> bool:
    generator = soup.find("meta", attrs={"name": "generator"})
    if generator and "wix.com" in (generator.get("content") or "").lower():
        return True
    return soup.find("script", id="wix-viewer-model") is not None or soup.find("script", id="wix-warmup-data") is not None


def parse_scripts(soup: BeautifulSoup) -> list[dict]:
    """The page's embedded JSON (read before the scripts are removed from the tree)."""
    return _json_scripts(soup)


def _json_scripts(soup: BeautifulSoup) -> list[dict]:
    found = []
    for script in soup.find_all("script", attrs={"type": "application/json"}):
        text = script.string or script.get_text()
        if "appsWarmupData" not in text:
            continue
        try:
            data = json.loads(text)
        except ValueError:
            continue
        if isinstance(data, dict):
            found.append(data)
    return found


def _key(file: str) -> str:
    return file.split("~")[0].split(".")[0]


def _item(raw: dict) -> GalleryItem | None:
    meta = raw.get("metaData") or {}
    alt = str(meta.get("alt") or meta.get("title") or "").strip()
    if meta.get("type") == "video":
        posters = [p for p in meta.get("posters") or [] if isinstance(p, dict) and p.get("url")]
        if not posters:
            return None
        best = max(posters, key=lambda p: int(p.get("width") or 0))
        return GalleryItem(f"https://static.wixstatic.com/media/{best['url']}", alt, int(best.get("width") or 0),
                           int(best.get("height") or 0), is_video_poster=True, source_key=_key(str(raw.get("mediaUrl") or best["url"])))
    media = raw.get("mediaUrl")
    if not isinstance(media, str) or not IMAGE_SUFFIX.search(media):
        return None
    return GalleryItem(f"https://static.wixstatic.com/media/{media}", alt, int(meta.get("width") or 0),
                       int(meta.get("height") or 0), source_key=_key(media))


def galleries(soup: BeautifulSoup, root: Tag, scripts: list[dict]) -> list[Gallery]:
    """Galleries whose component sits inside `root`, with the items Wix put into the page."""
    found: list[Gallery] = []
    seen: set[str] = set()
    for data in scripts:
        for app in (data.get("appsWarmupData") or {}).values():
            if not isinstance(app, dict):
                continue
            for key, value in app.items():
                match = COMPONENT_DATA.match(key)
                if not match or match.group(1) in seen or not isinstance(value, dict):
                    continue
                component = match.group(1)
                element = soup.find(id=component)
                if element is None or not (element is root or any(p is root for p in element.parents)):
                    continue  # header, footer or another page's gallery
                items = [i for i in (_item(raw) for raw in value.get("items") or [] if isinstance(raw, dict)) if i]
                settings = app.get(f"{component}_appSettings")
                gallery_id = str(settings.get("galleryId") or "") if isinstance(settings, dict) else ""
                seen.add(component)
                found.append(Gallery(component, items, int(value.get("totalItemsCount") or len(items)), gallery_id))
    return found


SITE_APP = "22bef345-3c5b-4c18-b782-74d4085112ff"  # the site's own app: its token is the one the viewer sends
PAGE_SIZE = 100
MAX_ITEMS = 2000


def _service_item(raw: dict) -> GalleryItem | None:
    """An item as the Pro Gallery service returns it (the page ships a different shape)."""
    alt = str(raw.get("alt") or raw.get("title") or "").strip()
    if raw.get("dataType") == "Video":
        meta = raw.get("videoMetadata") or {}
        posters = [p for p in meta.get("posters") or [] if isinstance(p, dict) and p.get("url")]
        if not posters:
            return None
        best = max(posters, key=lambda p: int(p.get("width") or 0))
        return GalleryItem(best["url"], alt, int(best.get("width") or 0), int(best.get("height") or 0),
                           is_video_poster=True, source_key=_key(media_id(best["url"]) or ""))
    url = str(raw.get("mediaUrl") or "")
    file = media_id(url)
    if not file or not IMAGE_SUFFIX.search(file):
        return None
    meta = raw.get("photoMetadata") or {}
    return GalleryItem(url, alt, int(meta.get("width") or 0), int(meta.get("height") or 0), source_key=_key(file))


def fetch_all_items(fetcher, origin: str, gallery: Gallery) -> list[GalleryItem] | None:
    """Every item of a gallery, by paging the same public service the page calls while it is scrolled.

    The page only ships the first items. The service wants the site's access token (public, from
    /_api/v1/access-tokens) in the Authorization header. Returns None when the service cannot be used,
    so the caller keeps what the page had.
    """
    if not gallery.gallery_id:
        return None
    tokens = fetcher.get(f"{origin}/_api/v1/access-tokens", max_bytes=2_000_000)
    if not tokens.ok:
        return None
    try:
        instance = json.loads(tokens.body)["apps"][SITE_APP]["instance"]
    except (ValueError, KeyError, TypeError):
        return None
    items: list[GalleryItem] = []
    total = min(gallery.total, MAX_ITEMS)
    offset = 0
    while offset < total:
        url = (f"{origin}/pro-gallery-webapp/v1/galleries/{gallery.gallery_id}?galleryId={gallery.gallery_id}"
               f"&offset={offset}&limit={PAGE_SIZE}&state=PUBLISHED")
        response = fetcher.get(url, max_bytes=5_000_000, headers={"Authorization": instance})
        if not response.ok:
            return items or None
        try:
            batch = json.loads(response.body)["gallery"]["items"]
        except (ValueError, KeyError, TypeError):
            return items or None
        if not batch:
            break
        items += [i for i in (_service_item(raw) for raw in batch if isinstance(raw, dict)) if i]
        offset += PAGE_SIZE
    return items


def add_missing_gallery_images(soup: BeautifulSoup, root: Tag, gallery: Gallery) -> int:
    """Appends an <img> to the gallery for every item that is not in its markup. Returns how many."""
    container = soup.find(id=gallery.component_id)
    if container is None:
        return 0
    present = {media_id(img.get("src") or "") for img in container.find_all("img")}
    present |= {media_id(src.split()[0]) for source in container.find_all("source")
                for src in (source.get("srcset") or "").split(",") if src.strip()}
    present.discard(None)
    added = 0
    for item in gallery.items:
        file = media_id(item.url)
        # The page may show another frame of a video than the largest poster in the data.
        if file in present or any(known.startswith(item.source_key) for known in present if known):
            continue
        image = soup.new_tag("img", src=item.url, alt=item.alt)
        container.append(image)
        present.add(file)
        added += 1
    return added
