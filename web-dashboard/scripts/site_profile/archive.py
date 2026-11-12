"""Wayback Machine: earliest and latest homepage capture. Non-blocking: short timeout, never fatal."""

from __future__ import annotations

import datetime as dt
import json
import urllib.parse
from concurrent.futures import ThreadPoolExecutor
from typing import Any

from .http import fetch

WAYBACK_CDX = "https://web.archive.org/cdx/search/cdx"
TIMEOUT = 6
MAX_BYTES = 3_000_000
NOTE = (
    "Captures are for the supplied and final homepage host variants only. An archive capture proves the site "
    "existed by then; it does not establish the exact launch or build date."
)


def empty(error: str | None = None, queried: list[str] | None = None) -> dict[str, Any]:
    return {
        "earliest_capture_utc": None, "earliest_capture_year": None, "earliest_capture_url": None,
        "earliest_capture_archive_url": None, "latest_capture_utc": None, "latest_capture_url": None,
        "latest_capture_archive_url": None, "queried_homepages": queried or [], "capture_rows_returned": 0,
        "source": WAYBACK_CDX, "error": error, "note": NOTE,
    }


def _homepages(hosts: list[str]) -> list[str]:
    urls: list[str] = []
    for host in hosts:
        if not host:
            continue
        try:
            host = host.encode("idna").decode("ascii").lower().rstrip(".")
        except UnicodeError:
            host = host.lower().rstrip(".")
        url = f"https://{f'[{host}]' if ':' in host else host}/"
        if url not in urls:
            urls.append(url)
    return urls


def _capture(url: str, limit: int) -> tuple[list[list[str]], str | None]:
    query = urllib.parse.urlencode([
        ("url", url), ("matchType", "exact"), ("output", "json"), ("filter", "statuscode:200"),
        ("filter", "mimetype:text/html"), ("fl", "timestamp,original"), ("limit", str(limit)), ("gzip", "false"),
    ])
    response = fetch(f"{WAYBACK_CDX}?{query}", timeout=TIMEOUT, max_bytes=MAX_BYTES)
    if response.error or not response.status or response.status >= 400:
        return [], response.error or f"HTTP {response.status}"
    if response.truncated:
        return [], f"CDX response exceeded {MAX_BYTES} bytes"
    try:
        rows = json.loads(response.body.decode("utf-8"))
        return [row for row in rows if isinstance(row, list) and len(row) >= 2 and str(row[0]).isdigit()], None
    except (UnicodeDecodeError, json.JSONDecodeError, TypeError) as exc:
        return [], f"Invalid CDX response: {exc}"


def profile_archive(hosts: list[str]) -> dict[str, Any]:
    homepages = _homepages(hosts)
    probes = [(url, limit) for url in homepages for limit in (1, -1)]
    rows: list[list[str]] = []
    errors: list[str] = []
    with ThreadPoolExecutor(max_workers=max(1, len(probes))) as pool:
        for (url, limit), (found, error) in zip(probes, pool.map(lambda p: _capture(*p), probes)):
            rows.extend(found)
            if error:
                errors.append(f"{url} (limit {limit}): {error}")

    dated: list[tuple[dt.datetime, str]] = []
    for row in rows:
        try:
            dated.append((dt.datetime.strptime(row[0], "%Y%m%d%H%M%S").replace(tzinfo=dt.timezone.utc), row[1]))
        except ValueError:
            continue
    if not dated:
        return empty("; ".join(errors) if errors else "No matching homepage captures returned", homepages)

    earliest, earliest_url = min(dated, key=lambda item: item[0])
    latest, latest_url = max(dated, key=lambda item: item[0])
    result = empty("; ".join(errors) if errors else None, homepages)
    result.update(
        earliest_capture_utc=earliest.isoformat(), earliest_capture_year=earliest.year,
        earliest_capture_url=earliest_url,
        earliest_capture_archive_url=f"https://web.archive.org/web/{earliest:%Y%m%d%H%M%S}/{earliest_url}",
        latest_capture_utc=latest.isoformat(), latest_capture_url=latest_url,
        latest_capture_archive_url=f"https://web.archive.org/web/{latest:%Y%m%d%H%M%S}/{latest_url}",
        capture_rows_returned=len(dated),
    )
    return result
