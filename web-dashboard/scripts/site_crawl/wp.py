"""WordPress content through the public REST API: only as many items as the page limit asks for."""

from __future__ import annotations

import json
import re
from typing import Iterator

from bs4 import BeautifulSoup

from .net import Fetcher


def detect(fetcher: Fetcher, origin: str) -> str | None:
    """Base URL for REST calls ('...' + '/wp/v2/posts'), or None. Sites with plain permalinks use ?rest_route=."""
    for base in (f"{origin}/wp-json", f"{origin}/?rest_route="):
        sep = "&" if "?" in base else "?"
        response = fetcher.get(f"{base}/wp/v2/posts{sep}per_page=1", max_bytes=300_000)
        if response.ok and response.body[:1] in (b"[", b"{"):
            try:
                if isinstance(json.loads(response.body), list):
                    return base
            except ValueError:
                continue
    return None


def items(fetcher: Fetcher, base: str, endpoint: str, limit: int) -> Iterator[dict]:
    """Posts or pages, newest first, fetched page by page and stopped as soon as `limit` items were yielded."""
    sep = "&" if "?" in base else "?"
    page, yielded = 1, 0
    while yielded < limit:
        response = fetcher.get(f"{base}/wp/v2/{endpoint}{sep}per_page={min(100, limit - yielded)}&page={page}&_embed=1", max_bytes=8_000_000)
        if response.status == 400 or not response.ok:  # past the last page, or the type is private
            return
        try:
            data = json.loads(response.body)
        except ValueError:
            return
        for item in data:
            if item.get("link"):
                yield item
                yielded += 1
                if yielded >= limit:
                    return
        total_pages = int(response.headers.get("x-wp-totalpages", "1") or 1)
        if page >= total_pages:
            return
        page += 1


def plain(html: str) -> str:
    return BeautifulSoup(html or "", "lxml").get_text(" ", strip=True)


def meta(item: dict) -> dict:
    """Author, categories and tags from an item fetched with _embed=1."""
    embedded = item.get("_embedded", {}) or {}
    author = next((a.get("name", "") for a in embedded.get("author", []) if isinstance(a, dict)), "")
    categories, tags = [], []
    for group in embedded.get("wp:term", []):
        for term in group:
            if isinstance(term, dict) and term.get("name"):
                (categories if term.get("taxonomy") == "category" else tags).append(term["name"])
    return {
        "published": (item.get("date") or "")[:10], "modified": (item.get("modified") or "")[:10],
        "author": author, "categories": categories, "tags": tags, "id": item.get("id"),
        "excerpt": re.sub(r"\s+", " ", plain(item.get("excerpt", {}).get("rendered", ""))).strip()[:300],
    }
