"""HTTP for the crawler: SSRF check on every hop, one shared rate limit, retries, size caps and robots.txt."""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass, field
from urllib.parse import urljoin
from urllib.robotparser import RobotFileParser

import httpx

from site_tools import netguard
from site_tools.cancel import CancelToken

from . import USER_AGENT

REDIRECTS = {301, 302, 303, 307, 308}
RETRY_STATUS = {429, 502, 503, 504}
ROBOTS_AGENT = "FreelanceDashboardCrawler"


@dataclass
class Resp:
    url: str
    status: int | None = None
    headers: dict[str, str] = field(default_factory=dict)
    body: bytes = b""
    error: str | None = None
    blocked: bool = False
    too_large: bool = False

    @property
    def ok(self) -> bool:
        return self.error is None and self.status == 200

    @property
    def content_type(self) -> str:
        return self.headers.get("content-type", "").split(";")[0].strip().lower()


class Fetcher:
    def __init__(self, timeout: float, delay: float, allow_private: bool, cancel: CancelToken, retries: int = 2) -> None:
        # Certificates are not verified: the crawler copies public content, and client sites often have broken chains.
        self._client = httpx.Client(
            verify=False,
            timeout=httpx.Timeout(timeout, connect=min(timeout, 10)),
            headers={"User-Agent": USER_AGENT, "Accept-Language": "en;q=0.9,*;q=0.5", "Accept-Encoding": "gzip, deflate"},
            follow_redirects=False,
        )
        self.delay = delay
        self.allow_private = allow_private
        self.cancel = cancel
        self.retries = retries
        self._lock = threading.Lock()
        self._next = 0.0

    def close(self) -> None:
        self._client.close()

    def _throttle(self) -> None:
        with self._lock:
            now = time.monotonic()
            wait = max(0.0, self._next - now)
            self._next = max(now, self._next) + self.delay
        if wait:
            time.sleep(wait)

    def get(self, url: str, max_bytes: int = 5_000_000) -> Resp:
        """GET with manual redirects (each hop checked), retries, and a hard cap on the body size."""
        result = Resp(url=url)
        for attempt in range(self.retries + 1):
            self.cancel.check()
            result = self._once(url, max_bytes)
            retryable = result.error is not None and not result.blocked and not result.too_large
            if result.blocked or result.too_large or not (retryable or result.status in RETRY_STATUS) or attempt == self.retries:
                return result
            time.sleep(1.5 * (attempt + 1))
        return result

    def _once(self, url: str, max_bytes: int) -> Resp:
        current = url
        try:
            for _ in range(6):
                try:
                    netguard.check_url(current, self.allow_private)
                except netguard.BlockedTarget as blocked:
                    return Resp(url=current, error=str(blocked), blocked=True)
                self._throttle()
                with self._client.stream("GET", current) as response:
                    location = response.headers.get("location")
                    if response.status_code in REDIRECTS and location:
                        current = urljoin(current, location)
                        continue
                    headers = {k.lower(): v.strip() for k, v in response.headers.items()}
                    declared = headers.get("content-length", "")
                    if declared.isdigit() and int(declared) > max_bytes:
                        return Resp(url=current, status=response.status_code, headers=headers, too_large=True)
                    chunks: list[bytes] = []
                    size = 0
                    for chunk in response.iter_bytes():
                        size += len(chunk)
                        if size > max_bytes:
                            return Resp(url=current, status=response.status_code, headers=headers, too_large=True)
                        chunks.append(chunk)
                    return Resp(url=current, status=response.status_code, headers=headers, body=b"".join(chunks))
            return Resp(url=current, error="Too many redirects.")
        except Exception as exc:  # noqa: BLE001 - every network failure becomes a result, not a crash
            return Resp(url=current, error=f"{type(exc).__name__}: {exc}")


class Robots:
    def __init__(self, parser: RobotFileParser | None = None) -> None:
        self._parser = parser

    @classmethod
    def load(cls, fetcher: Fetcher, origin: str) -> "Robots":
        response = fetcher.get(f"{origin}/robots.txt", max_bytes=500_000)
        if not response.ok or "html" in response.content_type:
            return cls()
        parser = RobotFileParser()
        parser.parse(response.body.decode("utf-8", errors="replace").splitlines())
        return cls(parser)

    def allowed(self, url: str) -> bool:
        return self._parser is None or self._parser.can_fetch(ROBOTS_AGENT, url)

    @property
    def sitemaps(self) -> list[str]:
        return list(self._parser.site_maps() or []) if self._parser else []

    @property
    def crawl_delay(self) -> float | None:
        delay = self._parser.crawl_delay(ROBOTS_AGENT) if self._parser else None
        return float(delay) if delay is not None else None
