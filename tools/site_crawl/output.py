"""The run folder: page files, manifest.json, structure.json, assets.json, urls.json and the agent-facing README."""

from __future__ import annotations

import json
import os
import re
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Any
from urllib.parse import unquote, urlparse

from . import SCHEMA_VERSION, TOOL_VERSION
from .assets import AssetStore
from .options import Options
from .urls import clean_url, key_of


def write_json_atomic(path: Path, data: Any) -> None:
    """Temp file plus rename: a reader (or a crash) never sees half a file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def write_text_atomic(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text, encoding="utf-8")
    os.replace(temporary, path)


@dataclass
class PageRecord:
    id: str
    url: str
    file: str  # relative to the run folder
    title: str
    description: str
    lang: str | None
    depth: int
    parent_id: str | None
    source: str  # html | wp-post | wp-page
    headings: list[dict]
    word_count: int
    links_internal: list[str]  # URLs, resolved to page ids when the crawl is finalised
    links_external: list[str]
    images: list[str]
    files: list[str]
    crawled_at: str
    notes: list[str] = field(default_factory=list)
    wp: dict | None = None

    def to_json(self, ids: dict[str, str]) -> dict[str, Any]:
        internal = []
        for link in self.links_internal:
            internal.append(ids.get(key_of(link), link))
        return {
            "id": self.id, "url": self.url, "file": self.file, "title": self.title, "description": self.description, "lang": self.lang,
            "depth": self.depth, "parent_id": self.parent_id, "source": self.source, "headings": self.headings,
            "word_count": self.word_count, "links_internal": list(dict.fromkeys(internal)), "links_external": self.links_external[:200],
            "images": self.images, "files": self.files, "notes": self.notes,
        }


def front_matter(page: PageRecord) -> str:
    def q(value: Any) -> str:
        return json.dumps(value, ensure_ascii=False)

    lines = ["---", f"id: {page.id}", f"url: {q(page.url)}", f"title: {q(page.title)}", f"description: {q(page.description)}",
             f"lang: {q(page.lang)}", f"source: {page.source}", f"depth: {page.depth}", f"crawled_at: {q(page.crawled_at)}",
             f"word_count: {page.word_count}"]
    if page.wp:
        lines.append("wp:")
        for key, value in page.wp.items():
            if value not in (None, "", []):
                lines.append(f"  {key}: {q(value)}")
    lines.append("---")
    return "\n".join(lines)


_MD_LINK = re.compile(r"(\]\()(<?)([^)\s>]+)(>?)(\))")


def rewrite_links(markdown: str, current_file: str, url_to_file: dict[str, str]) -> str:
    """Internal links to exported pages become relative .md links; others stay absolute."""
    here = PurePosixPath(current_file).parent

    def relative(target: str) -> str:
        return os.path.relpath(target, str(here)).replace(os.sep, "/")

    def replace(match: re.Match[str]) -> str:
        url = unquote(match.group(3))
        base, hash_, fragment = url.partition("#")
        if not base.startswith(("http://", "https://")):
            return match.group(0)
        target = url_to_file.get(key_of(clean_url(base)))  # tracking parameters do not make a different page
        if not target:
            return match.group(0)
        link = relative(target).replace(" ", "%20") + (hash_ + fragment if fragment else "")
        return f"{match.group(1)}{link}{match.group(5)}"

    return _MD_LINK.sub(replace, markdown)


def build_tree(pages: list[PageRecord]) -> dict[str, Any]:
    root: dict[str, Any] = {"name": "/", "page": None, "children": []}
    for page in pages:
        node = root
        segments = [unquote(s) for s in urlparse(page.url).path.strip("/").split("/") if s]
        for segment in segments:
            child = next((c for c in node["children"] if c["name"] == segment), None)
            if child is None:
                child = {"name": segment, "page": None, "children": []}
                node["children"].append(child)
            node = child
        node["page"] = page.id
    return root


class RunFolder:
    def __init__(self, opts: Options) -> None:
        self.opts = opts
        self.root = opts.output_dir
        self.pages: list[PageRecord] = []
        self.urls: dict[str, dict[str, Any]] = {}  # key -> entry
        self.log_path = self.root / "logs" / "crawl.log"

    def log(self, line: str) -> None:
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        with self.log_path.open("a", encoding="utf-8") as handle:
            handle.write(line.rstrip() + "\n")

    def url_state(self, url: str, state: str, reason: str | None = None) -> None:
        self.urls[key_of(url)] = {"url": url, "state": state, "reason": reason}

    def write_page(self, page: PageRecord, body: str) -> None:
        write_text_atomic(self.root / page.file, front_matter(page) + "\n\n" + body + "\n")

    def ids_by_key(self) -> dict[str, str]:
        return {key_of(p.url): p.id for p in self.pages}

    def write_indexes(self, assets: AssetStore, manifest: dict[str, Any]) -> None:
        ids = self.ids_by_key()
        write_json_atomic(self.root / "structure.json", {
            "schema_version": SCHEMA_VERSION, "site": self.opts.start_urls[0],
            "pages": [p.to_json(ids) for p in self.pages], "tree": build_tree(self.pages),
        })
        write_json_atomic(self.root / "assets.json", {"schema_version": SCHEMA_VERSION, "assets": [a.to_json() for a in assets.assets],
                                                       "failures": assets.failures[:500]})
        write_json_atomic(self.root / "urls.json", {"schema_version": SCHEMA_VERSION, "urls": list(self.urls.values())})
        write_json_atomic(self.root / "manifest.json", manifest)

    def finalize_links(self, assets: list | tuple = ()) -> None:
        """Second pass once every page is known: point links between exported pages at the .md files."""
        url_to_file = {key_of(p.url): p.file for p in self.pages}
        for asset in assets:  # links to downloaded documents point at the local copy too
            for url in [asset.url, *asset.aliases]:
                url_to_file.setdefault(key_of(url), asset.file)
        for page in self.pages:
            path = self.root / page.file
            if not path.is_file():
                continue
            text = path.read_text(encoding="utf-8")
            updated = rewrite_links(text, page.file, url_to_file)
            if updated != text:
                write_text_atomic(path, updated)

    def write_readme(self, manifest: dict[str, Any], assets: AssetStore) -> None:
        counts = manifest["counts"]
        empty = [p for p in self.pages if p.word_count < 20]
        top = self.pages[:60]
        lines = [
            f"# Content export of {self.opts.start_urls[0]}", "",
            f"Status: **{manifest['status']}**" + (f" ({manifest['stop_reason']})" if manifest.get("stop_reason") else ""),
            f"Crawled: {manifest['started_at']} to {manifest.get('finished_at') or 'still running'}", "",
            f"- {counts['pages']} pages as Markdown in `pages/`", f"- {counts['images']} images in `images/` (identical files stored once)",
            f"- {counts['files']} other files in `files/`", f"- {counts['errors']} failed requests, {counts['skipped']} skipped URLs", "",
            "## How to read this folder", "",
            "1. Start with `structure.json`: every page with its title, URL, headings, language, word count, internal links and the images it uses, plus a URL tree.",
            "2. Open the Markdown file named in `pages[].file`. Each file starts with YAML front matter (`url`, `title`, `description`, `lang`, `source`).",
            "3. `assets.json` lists every image and file with its size, dimensions, alt texts and the pages that use it (`used_on`). Images are linked from the pages with relative paths such as `../images/logo.png`.",
            "4. `urls.json` records every URL that was seen: crawled, skipped (with the reason), failed or external.",
            "5. `site.md` (when present) holds the header and footer text, which is the same on every page, once.",
            "6. Links between exported pages are relative `.md` links; links to pages that were not exported keep their original URL.", "",
        ]
        if empty:
            lines += ["## Pages with little or no text", "",
                      "These are probably rendered by JavaScript or are mostly images. The text may be missing.", ""]
            lines += [f"- [{p.title or p.url}]({p.file}) ({p.word_count} words)" for p in empty[:30]] + [""]
        lines += ["## Pages", ""]
        lines += [f"- [{p.title or p.url}]({p.file}) <{p.url}>" for p in top]
        if len(self.pages) > len(top):
            lines.append(f"- … and {len(self.pages) - len(top)} more in `structure.json`")
        failed = [u for u in self.urls.values() if u["state"] in ("failed", "skipped")]
        if failed:
            lines += ["", "## Skipped and failed URLs", ""]
            lines += [f"- {u['url']}: {u['reason']}" for u in failed[:60]]
            if len(failed) > 60:
                lines.append(f"- … and {len(failed) - 60} more in `urls.json`")
        if assets.failures:
            lines += ["", "## Files that could not be downloaded", ""]
            lines += [f"- {f['url']}: {f['reason']}" for f in assets.failures[:40]]
        write_text_atomic(self.root / "README.md", "\n".join(lines) + "\n")


def new_manifest(opts: Options, started_at: str) -> dict[str, Any]:
    return {
        "schema_version": SCHEMA_VERSION, "tool_version": TOOL_VERSION, "start_urls": opts.start_urls,
        "options": {
            "max_pages": opts.max_pages, "max_depth": opts.max_depth, "include_subdomains": opts.include_subdomains,
            "include": opts.include, "exclude": opts.exclude, "respect_robots": opts.respect_robots, "delay": opts.delay,
            "concurrency": opts.concurrency, "types": {"images": opts.images, "documents": opts.documents, "media": opts.media, "fonts": opts.fonts},
            "max_file_mb": opts.max_file_mb, "max_total_mb": opts.max_total_mb, "main_selector": opts.main_selector,
            "strip": opts.strip, "keep_chrome": opts.keep_chrome, "languages": opts.languages, "wordpress": opts.wordpress,
        },
        "started_at": started_at, "finished_at": None, "status": "running", "stop_reason": None,
        "counts": {"pages": 0, "images": 0, "files": 0, "bytes": 0, "errors": 0, "skipped": 0}, "errors": [],
    }
