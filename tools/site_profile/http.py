"""HTTP fetching with a redirect chain, SSRF checks on every hop, timing and a TLS-failure fallback."""

from __future__ import annotations

import re
import time
from dataclasses import dataclass, field
from typing import Any
from urllib.parse import urljoin

import httpx

from site_tools import netguard

from . import USER_AGENT

REDIRECT_STATUSES = {301, 302, 303, 307, 308}
DEFAULT_ACCEPT = "text/html,application/xhtml+xml,application/xml,application/json,*/*;q=0.5"


@dataclass
class Response:
    url: str
    status: int | None = None
    headers: dict[str, str] = field(default_factory=dict)
    set_cookies: list[str] = field(default_factory=list)
    body: bytes = b""
    truncated: bool = False
    redirects: list[dict[str, Any]] = field(default_factory=list)
    error: str | None = None
    error_code: str | None = None  # dns | timeout | tls | connect | blocked | redirects | other
    ttfb_ms: int | None = None
    total_ms: int | None = None
    http_version: str | None = None
    tls_verified: bool | None = None  # None for plain HTTP or when no connection was made
    tls_error: str | None = None

    @property
    def ok(self) -> bool:
        return self.error is None and self.status is not None


def classify_error(exc: Exception) -> tuple[str, str]:
    text = f"{type(exc).__name__}: {exc}"
    lowered = text.lower()
    if isinstance(exc, httpx.TimeoutException):
        return "timeout", text
    if any(marker in lowered for marker in ("name or service not known", "getaddrinfo", "nodename nor servname", "no address associated", "temporary failure in name resolution")):
        return "dns", text
    if "certificate" in lowered or "ssl" in lowered or "tls" in lowered:
        return "tls", text
    if isinstance(exc, httpx.TooManyRedirects):
        return "redirects", text
    if isinstance(exc, httpx.TransportError):
        return "connect", text
    return "other", text


def fetch_once(
    url: str,
    *,
    timeout: float,
    max_bytes: int,
    allow_private: bool = False,
    verify: bool = True,
    max_redirects: int = 10,
    follow_redirects: bool = True,
    guarded: bool = True,
    accept: str = DEFAULT_ACCEPT,
) -> Response:
    """One request chain. Redirects are followed by hand so each hop can be checked and recorded."""
    result = Response(url=url)
    started = time.perf_counter()
    current = url
    cookies: list[str] = []
    try:
        with httpx.Client(
            verify=verify,
            timeout=httpx.Timeout(timeout, connect=min(timeout, 8)),
            headers={"User-Agent": USER_AGENT, "Accept": accept, "Accept-Encoding": "gzip, deflate"},
            follow_redirects=False,
        ) as client:
            for hop in range(max_redirects + 1):
                if guarded:
                    try:
                        netguard.check_url(current, allow_private)
                    except netguard.BlockedTarget as blocked:
                        result.url = current
                        result.error, result.error_code = str(blocked), "blocked"
                        return _finish(result, started, cookies)
                hop_started = time.perf_counter()
                with client.stream("GET", current) as response:
                    ttfb = round((time.perf_counter() - hop_started) * 1000)
                    cookies.extend(response.headers.get_list("set-cookie"))
                    location = response.headers.get("location")
                    if follow_redirects and response.status_code in REDIRECT_STATUSES and location:
                        nxt = urljoin(current, location)
                        result.redirects.append({"status": response.status_code, "from": current, "to": nxt})
                        current = nxt
                        continue
                    chunks: list[bytes] = []
                    received = 0
                    for chunk in response.iter_bytes():
                        chunks.append(chunk)
                        received += len(chunk)
                        if received > max_bytes:
                            break
                    body = b"".join(chunks)
                    result.status = response.status_code
                    result.url = current
                    result.headers = {key.lower(): value.strip() for key, value in response.headers.items()}
                    result.body = body[:max_bytes]
                    result.truncated = len(body) > max_bytes
                    result.ttfb_ms = ttfb
                    result.http_version = response.http_version
                    result.tls_verified = True if current.startswith("https://") and verify else None
                    return _finish(result, started, cookies)
            result.error, result.error_code = f"Stopped after {max_redirects} redirects.", "redirects"
            result.url = current
    except Exception as exc:  # noqa: BLE001 - every failure becomes a structured error
        result.url = current
        result.error_code, result.error = classify_error(exc)
    return _finish(result, started, cookies)


def _finish(result: Response, started: float, cookies: list[str]) -> Response:
    result.total_ms = round((time.perf_counter() - started) * 1000)
    result.set_cookies = cookies
    return result


def fetch(url: str, **options: Any) -> Response:
    """Like fetch_once, but a certificate failure is retried without verification and flagged."""
    response = fetch_once(url, **options)
    if response.error_code == "tls" and options.get("verify", True):
        retry = fetch_once(url, **{**options, "verify": False})
        if retry.ok:
            retry.tls_verified = False
            retry.tls_error = response.error
            return retry
    return response


def decode_body(body: bytes, content_type: str = "") -> str:
    match = re.search(r"charset\s*=\s*[\"']?([^;\s\"']+)", content_type, re.IGNORECASE)
    encoding = match.group(1) if match else "utf-8"
    try:
        return body.decode(encoding, errors="replace")
    except LookupError:
        return body.decode("utf-8", errors="replace")
