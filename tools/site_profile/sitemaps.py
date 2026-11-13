"""robots.txt and sitemaps, tolerant of DOCTYPEs and following sitemap indexes within caps."""

from __future__ import annotations

import gzip
import html
import re
import urllib.parse
from typing import Any

from defusedxml import ElementTree as SafeET
from xml.etree.ElementTree import ParseError

from .http import decode_body, fetch
from .urls import get_origin, strip_www

MAX_FILES = 10
MAX_URLS_COUNTED = 50_000
MAX_URLS_KEPT = 500
MAX_SITEMAP_BYTES = 5_000_000

_DOCTYPE = re.compile(rb"<!DOCTYPE[^\[>]*(\[.*?\])?\s*>", re.IGNORECASE | re.DOTALL)


def empty() -> dict[str, Any]:
    return {
        "robots": {
            "url": None, "status": None, "error": None, "sitemap_directives": [],
            "allow_rule_count": 0, "disallow_rule_count": 0, "crawl_delay": None,
        },
        "sitemaps": {"files": [], "total_urls": 0, "urls": [], "truncated": False},
    }


def parse_robots(text: str) -> dict[str, Any]:
    info = {"sitemap_directives": [], "allow_rule_count": 0, "disallow_rule_count": 0, "crawl_delay": None}
    for line in text.splitlines():
        clean = line.split("#", 1)[0].strip()
        if ":" not in clean:
            continue
        directive, value = (part.strip() for part in clean.split(":", 1))
        directive = directive.lower()
        if directive == "sitemap" and value:
            info["sitemap_directives"].append(value)
        elif directive == "allow" and value:
            info["allow_rule_count"] += 1
        elif directive == "disallow" and value:
            info["disallow_rule_count"] += 1
        elif directive == "crawl-delay" and info["crawl_delay"] is None:
            try:
                info["crawl_delay"] = float(value)
            except ValueError:
                pass
    info["sitemap_directives"] = info["sitemap_directives"][:20]
    return info


def parse_sitemap(body: bytes) -> dict[str, Any]:
    """Returns {kind, entries: [(loc, lastmod)]}. Raises ParseError/ValueError on unusable input."""
    if body[:2] == b"\x1f\x8b":
        body = gzip.decompress(body)[:MAX_SITEMAP_BYTES]
    body = _DOCTYPE.sub(b"", body.lstrip(b"\xef\xbb\xbf"))
    try:
        root = SafeET.fromstring(body)
    except ParseError:
        return _parse_lenient(body)  # real-world sitemaps often contain unescaped "&" and similar
    kind = root.tag.split("}")[-1].lower()
    if kind not in {"urlset", "sitemapindex"}:
        raise ValueError(f"Unexpected root element <{kind}>")
    entries: list[tuple[str, str | None]] = []
    for node in root:
        loc = lastmod = None
        for child in node:
            name = child.tag.split("}")[-1].lower()
            if name == "loc" and child.text:
                loc = child.text.strip()
            elif name == "lastmod" and child.text:
                lastmod = child.text.strip()
        if loc:
            entries.append((loc, lastmod))
    return {"kind": kind, "entries": entries}


def _parse_lenient(body: bytes) -> dict[str, Any]:
    text = body.decode("utf-8", errors="replace")
    kind = "sitemapindex" if re.search(r"<sitemapindex\b", text, re.I) else "urlset"
    entries: list[tuple[str, str | None]] = []
    for block in re.findall(r"<(?:url|sitemap)\b[^>]*>(.*?)</(?:url|sitemap)>", text, re.I | re.S):
        loc = re.search(r"<loc>\s*(.*?)\s*</loc>", block, re.I | re.S)
        mod = re.search(r"<lastmod>\s*(.*?)\s*</lastmod>", block, re.I | re.S)
        if loc and loc.group(1):
            entries.append((html.unescape(loc.group(1)), html.unescape(mod.group(1)) if mod else None))
    if not entries:
        raise ValueError("not well-formed XML and no <loc> entries found")
    return {"kind": kind, "entries": entries, "lenient": True}


def _same_site(url: str, origin_host: str) -> bool:
    return strip_www((urllib.parse.urlsplit(url).hostname or "").lower()) == strip_www(origin_host.lower())


def profile_sitemaps(final_url: str, timeout: float, allow_private: bool = False) -> tuple[dict[str, Any], list[dict[str, str]]]:
    """Returns (section, errors)."""
    section = empty()
    errors: list[dict[str, str]] = []
    origin = get_origin(final_url)
    origin_host = urllib.parse.urlsplit(origin).hostname or ""
    robots_url = urllib.parse.urljoin(origin + "/", "robots.txt")
    options = {"timeout": timeout, "allow_private": allow_private}

    robots = fetch(robots_url, max_bytes=750_000, **options)
    section["robots"].update(url=robots_url, status=robots.status, error=robots.error)
    queue: list[str] = []
    if robots.status and 200 <= robots.status < 300:
        parsed = parse_robots(decode_body(robots.body, robots.headers.get("content-type", "")))
        section["robots"].update(parsed)
        queue = [urllib.parse.urljoin(robots_url, u) for u in parsed["sitemap_directives"]]
    elif robots.error:
        errors.append({"section": "sitemaps", "code": robots.error_code or "error", "message": f"robots.txt: {robots.error}"})
    if not queue:
        queue = [urllib.parse.urljoin(origin + "/", "sitemap.xml")]

    files: list[dict[str, Any]] = []
    urls: list[dict[str, Any]] = []
    seen: set[str] = set()
    total = 0
    while queue and len(files) < MAX_FILES and total < MAX_URLS_COUNTED:
        url = queue.pop(0)
        if url in seen:
            continue
        seen.add(url)
        entry: dict[str, Any] = {"url": url, "status": None, "kind": None, "urls": None, "child_sitemaps": None, "error": None, "warning": None}
        files.append(entry)
        if not _same_site(url, origin_host):
            entry["error"] = "Skipped sitemap on a different host."
            continue
        response = fetch(url, max_bytes=MAX_SITEMAP_BYTES, **options)
        entry.update(status=response.status, error=response.error)
        if not (response.status and 200 <= response.status < 300):
            if response.status and response.status != 404:
                entry["error"] = entry["error"] or f"HTTP {response.status}"
            continue
        try:
            parsed_map = parse_sitemap(response.body)
        except (ParseError, ValueError, OSError, EOFError) as exc:
            snippet = response.body[:160].decode("utf-8", errors="replace").replace("\n", " ").strip()
            entry["warning"] = f"The sitemap cannot be read ({exc}). It starts with: {snippet!r}"
            continue
        entry["kind"] = parsed_map["kind"]
        if parsed_map.get("lenient"):
            entry["warning"] = "The sitemap is not valid XML (for example an unescaped &); URLs were read leniently."
        if parsed_map["kind"] == "sitemapindex":
            entry["child_sitemaps"] = len(parsed_map["entries"])
            queue.extend(loc for loc, _ in parsed_map["entries"])
        else:
            entry["urls"] = len(parsed_map["entries"])
            total += len(parsed_map["entries"])
            for loc, lastmod in parsed_map["entries"]:
                if len(urls) < MAX_URLS_KEPT:
                    urls.append({"loc": loc, "lastmod": lastmod})
    section["sitemaps"].update(
        files=files, total_urls=total, urls=urls,
        truncated=bool(queue) or total > len(urls),
    )
    return section, errors
