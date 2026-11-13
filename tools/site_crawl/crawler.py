"""The crawl loop: seeds, queue, fetching in small parallel batches, one page at a time through the extractor."""

from __future__ import annotations

import datetime as dt
import hashlib
import re
from collections import deque
from concurrent.futures import ThreadPoolExecutor
from urllib.parse import unquote, urljoin, urlparse

from site_profile.sitemaps import parse_sitemap
from site_tools import exitcodes, netguard
from site_tools.cancel import Cancelled, CancelToken
from site_tools.events import Emitter

from . import wp
from .assets import AssetStore
from .extract import PageExtractor, ignore_matcher
from .net import Fetcher, Robots
from .options import Options
from .output import PageRecord, RunFolder, new_manifest
from .urls import base_host, clean_url, host_allowed, key_of, page_slug, split_lang

FLUSH_EVERY = 10
MAX_SITEMAP_URLS = 5000
MAX_ERRORS_LISTED = 100
# Added to `exclude` when the WordPress REST API supplies the content: these are archives and system URLs.
WORDPRESS_EXCLUDES = [r"^/wp-(json|admin|login|content|includes)", r"xmlrpc\.php", r"/feed/?$", r"/(category|tag|author)/", r"/page/\d+/?$", r"[?&](s|p|replytocom)="]


def now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


class Crawl:
    def __init__(self, opts: Options, emit: Emitter, cancel: CancelToken) -> None:
        self.opts, self.emit, self.cancel = opts, emit, cancel
        self.base = base_host(opts.start_urls[0])
        self.fetcher = Fetcher(opts.timeout, opts.delay, opts.allow_private, cancel)
        self.assets = AssetStore(opts, self.fetcher, ignore_matcher())
        self.extractor = PageExtractor(opts, self.assets, self.base)
        self.folder = RunFolder(opts)
        self.manifest = new_manifest(opts, now())
        self.robots = Robots()
        self.queue: deque[tuple[str, int, str | None]] = deque()
        self.seen: set[str] = set()
        self.files_used: set[str] = set()
        self.digests: dict[str, str] = {}
        self.default_lang: str | None = None
        self.skipped = 0
        self.errors = 0
        self.stop_reason: str | None = None
        self.include = [re.compile(p, re.I) for p in opts.include]
        self.exclude = [re.compile(p, re.I) for p in opts.exclude]

    # ------------------------------------------------------------------ helpers

    def log(self, level: str, message: str) -> None:
        self.folder.log(f"{now()} {level.upper():5} {message}")
        self.emit.log(level, message)

    def in_scope(self, url: str) -> bool:
        if not host_allowed(url, self.base, self.opts.include_subdomains):
            return False
        parts = urlparse(url)
        target = unquote(parts.path) + (f"?{parts.query}" if parts.query else "")
        if any(p.search(target) for p in self.exclude):
            return False
        return not self.include or any(p.search(target) for p in self.include)

    def enqueue(self, url: str, depth: int, parent: str | None, entry_point: bool = False) -> None:
        key = key_of(url)
        if key in self.seen:
            return
        if not entry_point and not self.in_scope(url):
            if not host_allowed(url, self.base, self.opts.include_subdomains):
                if len(self.folder.urls) < 5000:
                    self.folder.url_state(url, "external")
            else:
                self.folder.url_state(url, "skipped", "excluded by the include/exclude patterns")
                self.skipped += 1
            self.seen.add(key)
            return
        self.seen.add(key)
        if self.opts.respect_robots and not self.robots.allowed(url):
            self.folder.url_state(url, "skipped", "disallowed by robots.txt")
            self.skipped += 1
            return
        self.queue.append((url, depth, parent))

    def file_for(self, url: str) -> tuple[str, str | None, str]:
        """(relative file, language prefix, '../' prefix back to the run folder root)."""
        parts = urlparse(url)
        prefix, rest = split_lang(parts.path, self.opts.languages)
        slug = page_slug(rest, parts.query)
        folder = f"pages/{prefix}" if prefix else "pages"
        name, counter = slug, 2
        while f"{folder}/{name}.md".lower() in self.files_used:
            name, counter = f"{slug}-{counter}", counter + 1
        file = f"{folder}/{name}.md"
        self.files_used.add(file.lower())
        return file, prefix, "../" * (2 if prefix else 1)

    def progress(self, label: str = "") -> None:
        done = len(self.folder.pages)
        total = min(self.opts.max_pages, max(done + len(self.queue), done, 1))
        self.emit.progress(done, total, label[:120])
        self.emit.stats(pages=done, images=sum(1 for a in self.assets.assets if a.kind == "image"),
                        files=sum(1 for a in self.assets.assets if a.kind != "image"), bytes=self.assets.total_bytes, errors=self.errors)

    def flush(self, final: bool = False) -> None:
        counts = self.manifest["counts"]
        counts.update(
            pages=len(self.folder.pages), images=sum(1 for a in self.assets.assets if a.kind == "image"),
            files=sum(1 for a in self.assets.assets if a.kind != "image"), bytes=self.assets.total_bytes, errors=self.errors, skipped=self.skipped,
        )
        self.folder.write_indexes(self.assets, self.manifest)

    def record_error(self, url: str, reason: str) -> None:
        self.errors += 1
        self.folder.url_state(url, "failed", reason)
        if len(self.manifest["errors"]) < MAX_ERRORS_LISTED:
            self.manifest["errors"].append({"url": url, "reason": reason})
        self.log("warn", f"{url}: {reason}")

    # ------------------------------------------------------------------ pages

    def add_page(self, url: str, depth: int, parent: str | None, source: str, *, html: bytes | str, fragment: bool = False,
                 title: str = "", description: str = "", wp_meta: dict | None = None) -> None:
        page_id = f"p{len(self.folder.pages) + 1}"
        file, prefix, up = self.file_for(url)
        extracted = self.extractor.extract(html, url, page_id, up, fragment=fragment)
        if self.default_lang is None and not fragment:
            self.default_lang = extracted.lang or "default"
        record = PageRecord(
            id=page_id, url=url, file=file, title=title or extracted.title or url, description=description or extracted.description,
            lang=prefix or extracted.lang or self.default_lang, depth=depth, parent_id=parent, source=source,
            headings=extracted.headings, word_count=extracted.word_count, links_internal=extracted.internal,
            links_external=extracted.external, images=extracted.images, files=extracted.files, crawled_at=now(),
            notes=extracted.notes, wp=wp_meta,
        )
        body = extracted.body
        if fragment and record.title and not body.lstrip().startswith("# "):
            body = f"# {record.title}\n\n{body}"
        self.folder.write_page(record, body)
        self.folder.pages.append(record)
        self.folder.url_state(url, "crawled")
        self.log("info", f"[{source}] {url} -> {file} ({len(extracted.images)} images)")
        if self.opts.max_depth is None or depth < self.opts.max_depth:
            for kind, target in extracted.discovered:
                if kind == "page":
                    self.enqueue(target, depth + 1, page_id)
        self.progress(urlparse(url).path or "/")
        if len(self.folder.pages) % FLUSH_EVERY == 0:
            self.flush()

    def handle_response(self, url: str, depth: int, parent: str | None, response) -> None:
        if response.blocked:
            self.record_error(url, response.error or "blocked by the network policy")
            return
        if response.error:
            self.record_error(url, response.error)
            return
        if response.status != 200:
            reason = f"HTTP {response.status}"
            if (response.status or 0) >= 500:
                self.record_error(url, reason)
            else:
                self.folder.url_state(url, "skipped", reason)
                self.skipped += 1
            return
        if "html" not in response.content_type and response.content_type:
            self.folder.url_state(url, "skipped", f"not a web page ({response.content_type})")
            self.skipped += 1
            return
        final = clean_url(response.url)
        if not host_allowed(final, self.base, self.opts.include_subdomains):
            self.folder.url_state(url, "skipped", f"redirects to {final}, outside the crawl scope")
            self.skipped += 1
            return
        final_key = key_of(final)
        if final_key != key_of(url):
            if final_key in {key_of(p.url) for p in self.folder.pages}:
                self.folder.url_state(url, "skipped", "redirects to a page that was already crawled")
                self.skipped += 1
                return
            self.seen.add(final_key)
        digest = hashlib.md5(response.body).hexdigest()
        if digest in self.digests:
            # The server answers unknown URLs with the same page instead of a 404.
            self.folder.url_state(url, "skipped", f"identical to {self.digests[digest]} (server catch-all)")
            self.skipped += 1
            return
        self.digests[digest] = final
        self.add_page(final, depth, parent, "html", html=response.body)

    # ------------------------------------------------------------------ seeds

    def sitemap_urls(self, origin: str) -> list[str]:
        pending = [*self.robots.sitemaps, f"{origin}/sitemap.xml"]
        done: set[str] = set()
        urls: list[str] = []
        while pending and len(done) < 20 and len(urls) < MAX_SITEMAP_URLS:
            sitemap = pending.pop(0)
            if sitemap in done:
                continue
            done.add(sitemap)
            response = self.fetcher.get(sitemap, max_bytes=8_000_000)
            if not response.ok:
                continue
            try:
                parsed = parse_sitemap(response.body)
            except Exception:  # noqa: BLE001 - a broken sitemap just adds nothing
                continue
            for loc, _ in parsed["entries"]:
                if parsed["kind"] == "sitemapindex":
                    pending.append(loc)
                else:
                    urls.append(clean_url(urljoin(sitemap, loc)))  # tolerate relative <loc> values
        return urls[:MAX_SITEMAP_URLS]

    def export_wordpress(self, base: str) -> None:
        wp_skip = [re.compile(p, re.I) for p in WORDPRESS_EXCLUDES]
        self.exclude.extend(wp_skip)  # archives and system URLs are not crawled when the API supplies the content
        for endpoint, source in (("pages", "wp-page"), ("posts", "wp-post")):
            remaining = self.opts.max_pages - len(self.folder.pages)
            if remaining <= 0:
                return
            for item in wp.items(self.fetcher, base, endpoint, remaining):
                self.cancel.check()
                link = clean_url(item["link"])
                if not host_allowed(link, self.base, self.opts.include_subdomains) or key_of(link) in self.seen:
                    continue
                self.seen.add(key_of(link))
                self.add_page(link, 1, None, source, html=f"<div>{item.get('content', {}).get('rendered', '')}</div>", fragment=True,
                              title=wp.plain(item.get("title", {}).get("rendered", "")), description=wp.meta(item)["excerpt"], wp_meta=wp.meta(item))
                if self.assets.cap_reached or len(self.folder.pages) >= self.opts.max_pages:
                    return

    # ------------------------------------------------------------------ run

    def run(self) -> int:
        opts = self.opts
        opts.output_dir.mkdir(parents=True, exist_ok=True)
        first = clean_url(opts.start_urls[0])
        origin = f"{urlparse(first).scheme}://{urlparse(first).netloc}"
        try:
            netguard.check_url(first, opts.allow_private)
        except netguard.BlockedTarget as blocked:
            self.manifest.update(status="failed", stop_reason="blocked", finished_at=now())
            self.manifest["errors"].append({"url": first, "reason": str(blocked)})
            self.log("error", str(blocked))
            self.flush(True)
            return exitcodes.BLOCKED

        status = "done"
        try:
            self.emit.progress(0, None, "starting")
            if opts.respect_robots:
                self.robots = Robots.load(self.fetcher, origin)
                if self.robots.crawl_delay and self.robots.crawl_delay > self.fetcher.delay:
                    self.log("info", f"robots.txt asks for a {self.robots.crawl_delay:g}s crawl delay; using it")
                    self.fetcher.delay = self.robots.crawl_delay

            wp_base = wp.detect(self.fetcher, origin) if opts.wordpress != "off" else None
            if wp_base:
                self.log("info", f"WordPress REST API found: {wp_base}")
            if opts.wordpress == "only" and not wp_base:
                self.log("warn", "WordPress REST API not found; crawling the HTML pages instead")

            for start in opts.start_urls:
                self.enqueue(clean_url(start), 0, None, entry_point=True)
            self.drain_entry_points()  # the pages you asked for always come first, before the page limit is spent on WordPress items
            if wp_base:
                self.export_wordpress(wp_base)
            if not (opts.wordpress == "only" and wp_base):
                for url in self.sitemap_urls(origin):
                    self.enqueue(url, 1, None)
                self.loop()
        except Cancelled:
            status = "canceled"
            self.stop_reason = "canceled"
        finally:
            self.fetcher.close()

        counts_pages = len(self.folder.pages)
        if self.assets.cap_reached:
            self.stop_reason = self.stop_reason or "size_cap"
        elif self.stop_reason is None and counts_pages >= opts.max_pages and self.queue:
            self.stop_reason = "max_pages"
        if status != "canceled":
            if counts_pages == 0:
                status = "failed"
            elif self.assets.cap_reached or self.errors:
                status = "partial"
        self.manifest.update(status=status, stop_reason=self.stop_reason, finished_at=now())
        self.folder.finalize_links(self.assets.assets)
        self.flush(True)
        self.folder.write_readme(self.manifest, self.assets)
        return {"done": exitcodes.OK, "partial": exitcodes.PARTIAL, "failed": exitcodes.FAILED, "canceled": exitcodes.CANCELLED}[status]

    def drain_entry_points(self) -> None:
        for _ in range(len(self.queue)):
            url, depth, parent = self.queue.popleft()
            self.handle_response(url, depth, parent, self.fetcher.get(url))

    def loop(self) -> None:
        opts = self.opts
        with ThreadPoolExecutor(max_workers=opts.concurrency) as pool:
            while self.queue and len(self.folder.pages) < opts.max_pages and not self.assets.cap_reached:
                self.cancel.check()
                batch = [self.queue.popleft() for _ in range(min(opts.concurrency, opts.max_pages - len(self.folder.pages), len(self.queue)))]
                fetched = list(pool.map(lambda item: (item, self.fetcher.get(item[0])), batch))
                for (url, depth, parent), response in fetched:
                    if len(self.folder.pages) >= opts.max_pages or self.assets.cap_reached:
                        self.queue.appendleft((url, depth, parent))
                        continue
                    self.handle_response(url, depth, parent, response)
