"""Clues about when the current design was built, from dated asset paths, file timestamps and copyright lines.

None of these is proof (a logo uploaded years ago is reused by a redesign), so every clue is reported with its
strength and the estimate is only the oldest and newest dated evidence.
"""

from __future__ import annotations

import datetime as dt
import email.utils
import re
import urllib.parse
from collections import Counter
from typing import Any

from bs4 import BeautifulSoup

from .http import fetch_once
from .urls import get_host, strip_www

MAX_ASSET_PROBES = 6
THIS_YEAR = dt.datetime.now(dt.timezone.utc).year

WP_UPLOADS = re.compile(r"/wp-content/uploads/(?:sites/\d+/)?((?:19|20)\d{2})/(0[1-9]|1[0-2])/")
DATED_ASSET = re.compile(
    r"""[^"'()\s=]*/((?:19|20)\d{2})[/-](0[1-9]|1[0-2])/[^"'()\s]*\.(?:jpe?g|png|gif|webp|svg|avif|css|js|mp4|pdf)""", re.I
)
STAMP_QUERY = re.compile(r"[?&](?:ver|v|version|t|ts|_)=(\d{10}|20\d{6})(?![\d.])")
COPYRIGHT = re.compile(r"(?:©|&copy;|&#169;|copyright)\s*(?:\(c\)\s*)?((?:19|20)\d{2})(?:\s*[-–—]\s*((?:19|20)\d{2}))?", re.I)

CONTEXT_KINDS = {"archive", "domain"}


def _clue(kind: str, label: str, date: str, detail: str | None, strength: str) -> dict[str, Any]:
    return {"kind": kind, "label": label, "date": date, "detail": detail, "strength": strength}


def _key(date: str) -> str:
    """Sortable form of YYYY, YYYY-MM or YYYY-MM-DD."""
    return {4: date + "-00-00", 7: date + "-00", 10: date}.get(len(date), date)


def empty() -> dict[str, Any]:
    return {"estimated_from": None, "latest_asset": None, "confidence": None, "basis": [], "clues": []}


def _from_html(html: str) -> list[dict[str, Any]]:
    clues: list[dict[str, Any]] = []

    uploads = Counter((m.group(1), m.group(2)) for m in WP_UPLOADS.finditer(html))
    if uploads:
        first, last = min(uploads), max(uploads)
        total = sum(uploads.values())
        clues.append(_clue("wp-uploads", "Oldest WordPress upload on the home page", f"{first[0]}-{first[1]}",
                           f"/wp-content/uploads/{first[0]}/{first[1]}/ ({total} upload references)", "strong"))
        if last != first:
            clues.append(_clue("wp-uploads", "Newest WordPress upload on the home page", f"{last[0]}-{last[1]}",
                               f"/wp-content/uploads/{last[0]}/{last[1]}/", "medium"))
    else:
        dated = Counter((m.group(1), m.group(2)) for m in DATED_ASSET.finditer(html) if int(m.group(1)) <= THIS_YEAR)
        if dated:
            first, last = min(dated), max(dated)
            clues.append(_clue("dated-path", "Oldest dated file path in the page", f"{first[0]}-{first[1]}",
                               f".../{first[0]}/{first[1]}/... ({sum(dated.values())} references)", "medium"))
            if last != first:
                clues.append(_clue("dated-path", "Newest dated file path in the page", f"{last[0]}-{last[1]}", None, "weak"))

    stamps: list[dt.date] = []
    for m in STAMP_QUERY.finditer(html):
        raw = m.group(1)
        try:
            day = (dt.datetime.fromtimestamp(int(raw), dt.timezone.utc).date() if len(raw) == 10
                   else dt.datetime.strptime(raw, "%Y%m%d").date())
        except (ValueError, OverflowError, OSError):
            continue
        if dt.date(2005, 1, 1) <= day <= dt.datetime.now(dt.timezone.utc).date():
            stamps.append(day)
    if stamps:
        clues.append(_clue("version-stamp", "Oldest dated asset version (?ver=)", min(stamps).isoformat(), f"{len(stamps)} versioned assets", "medium"))

    text = re.sub(r"<(script|style)\b.*?</\1>", " ", html, flags=re.I | re.S)
    text = re.sub(r"<[^>]+>", " ", text)
    years = [(int(m.group(1)), int(m.group(2) or m.group(1))) for m in COPYRIGHT.finditer(text)]
    years = [(a, b) for a, b in years if a <= THIS_YEAR and b <= THIS_YEAR + 1]
    if years:
        start = min(a for a, _ in years)
        clues.append(_clue("copyright", "Copyright year in the footer", str(start), "A hand-written year usually dates the build; ranges show the start year", "weak"))
    return clues


def _asset_candidates(html: str, final_url: str) -> list[str]:
    soup = BeautifulSoup(html, "html.parser")
    host = strip_www(get_host(final_url).lower())

    def same_site(url: str) -> bool:
        return strip_www((get_host(url) or "").lower()) == host

    def weight(url: str) -> int:
        path = urllib.parse.urlsplit(url).path.lower()
        return -sum(marker in path for marker in ("theme", "style", "main", "app", "custom", "site", "assets"))

    styles = [urllib.parse.urljoin(final_url, str(t["href"])) for t in soup.find_all("link", href=True)
              if "stylesheet" in [r.lower() for r in (t.get("rel") or [])]]
    scripts = [urllib.parse.urljoin(final_url, str(t["src"])) for t in soup.find_all("script", src=True)]
    images = [urllib.parse.urljoin(final_url, str(t["src"])) for t in soup.find_all("img", src=True)]
    ordered: list[str] = []
    for group, limit in ((sorted((u for u in styles if same_site(u)), key=weight), 3),
                         (sorted((u for u in scripts if same_site(u) and "jquery" not in u.lower()), key=weight), 2),
                         ([u for u in images if same_site(u)], 1)):
        for url in group[:limit]:
            if url not in ordered:
                ordered.append(url)
    return ordered[:MAX_ASSET_PROBES]


def _last_modified(urls: list[str], timeout: float, allow_private: bool) -> list[dict[str, Any]]:
    clues: list[dict[str, Any]] = []
    now = dt.datetime.now(dt.timezone.utc)
    for url in urls:
        response = fetch_once(url, timeout=min(timeout, 6), max_bytes=1, allow_private=allow_private, follow_redirects=True)
        if not response.ok or (response.status or 500) >= 400:
            continue
        header = response.headers.get("last-modified")
        if not header:
            continue
        try:
            modified = email.utils.parsedate_to_datetime(header)
        except (TypeError, ValueError):
            continue
        if modified.tzinfo is None:
            modified = modified.replace(tzinfo=dt.timezone.utc)
        if abs((now - modified).total_seconds()) < 2 * 86400:
            continue  # generated on the fly or just redeployed: says nothing about the design
        name = urllib.parse.urlsplit(url).path.rsplit("/", 1)[-1] or url
        kind = "stylesheet" if name.lower().endswith(".css") else "script" if name.lower().endswith(".js") else "image"
        clues.append(_clue("last-modified", f"{kind.capitalize()} last modified: {name}", modified.date().isoformat(), "Last-Modified header of the file on the server",
                           "strong" if kind == "stylesheet" else "medium"))
    return clues


def summarize(clues: list[dict[str, Any]]) -> dict[str, Any]:
    result = empty()
    result["clues"] = sorted(clues, key=lambda c: _key(c["date"]))
    dated = [c for c in clues if c["kind"] not in CONTEXT_KINDS and c["strength"] in {"strong", "medium"}]
    if not dated:
        return result
    result["estimated_from"] = min((c["date"] for c in dated), key=_key)
    result["latest_asset"] = max((c["date"] for c in dated), key=_key)
    kinds = sorted({c["kind"] for c in dated})
    result["basis"] = kinds
    result["confidence"] = "medium" if len(kinds) >= 2 else "low"
    return result


def collect(html: str, final_url: str, timeout: float, allow_private: bool = False) -> dict[str, Any]:
    clues = _from_html(html)
    clues.extend(_last_modified(_asset_candidates(html, final_url), timeout, allow_private))
    return summarize(clues)


def add_context(design: dict[str, Any], archive_first: str | None, domain_created: str | None, sitemap_lastmods: list[str]) -> dict[str, Any]:
    """Adds what is known about the site's age from other sections, then recomputes the summary."""
    clues = [c for c in design["clues"] if c["kind"] not in CONTEXT_KINDS | {"sitemap"}]
    if archive_first:
        clues.append(_clue("archive", "First Wayback capture", archive_first[:10], "The site existed by then; not necessarily this design", "context"))
    if domain_created:
        clues.append(_clue("domain", "Domain registered", domain_created[:10], "Upper bound on the age of any site on this domain", "context"))
    valid = sorted(m[:10] for m in sitemap_lastmods if re.match(r"^\d{4}-\d{2}-\d{2}", m or ""))
    if valid:
        clues.append(_clue("sitemap", "Oldest page modification in the sitemap", valid[0], f"{len(valid)} dated URLs; the newest is {valid[-1]}", "weak"))
    return summarize(clues)
