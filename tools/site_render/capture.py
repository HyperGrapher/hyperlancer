"""One page, two viewports: screenshots, load time and the list of requests."""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any
from urllib.parse import urlsplit

from playwright.sync_api import Page

from site_tools import netguard
from site_tools.cancel import CancelToken

DESKTOP = {"width": 1440, "height": 900}
MOBILE = {"width": 390, "height": 844}
MOBILE_AGENT = ("Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) AppleWebKit/605.1.15 "
                "(KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1")
MAX_FULLPAGE_HEIGHT = 12000  # a very long page would make a huge picture
MAX_REQUESTS = 400
JPEG_QUALITY = 80


@dataclass
class Capture:
    title: str = ""
    final_url: str = ""
    load_ms: int = 0
    requests: list[str] = field(default_factory=list)
    files: dict[str, str] = field(default_factory=dict)


def guard_requests(page: Page, allow_private: bool, blocked: list[str]) -> None:
    """The page can ask for anything, so every request gets the same network policy as the other tools."""
    if allow_private:
        return

    def handle(route: Any) -> None:
        url = route.request.url
        if urlsplit(url).scheme in ("data", "blob", "about"):
            route.continue_()
            return
        try:
            netguard.check_url(url, allow_private=False)
        except netguard.BlockedTarget:
            blocked.append(url)
            route.abort()
            return
        route.continue_()

    page.route("**/*", handle)


def settle(page: Page, seconds: float, cancel: CancelToken) -> None:
    """Late tags (Tag Manager, consent scripts) fire a few seconds after load."""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        cancel.check()
        page.wait_for_timeout(250)


def scroll_through(page: Page) -> None:
    """Lazy images only load when they come into view."""
    height = page.evaluate("document.documentElement.scrollHeight")
    position = 0
    while position < min(int(height), MAX_FULLPAGE_HEIGHT):
        position += 700
        page.evaluate(f"window.scrollTo(0, {position})")
        page.wait_for_timeout(120)
    page.evaluate("window.scrollTo(0, 0)")
    page.wait_for_timeout(200)


def shoot(page: Page, path: Path, full_page: bool) -> None:
    page.screenshot(path=str(path), type="jpeg", quality=JPEG_QUALITY, full_page=full_page)


def capture_site(browser: Any, url: str, out: Path, settle_seconds: float, allow_private: bool,
                 cancel: CancelToken, progress: Any) -> tuple[Capture, list[str]]:
    result = Capture()
    blocked: list[str] = []

    progress(1, 4, "desktop")
    desktop = browser.new_context(viewport=DESKTOP, ignore_https_errors=True)
    page = desktop.new_page()
    guard_requests(page, allow_private, blocked)
    page.on("request", lambda request: result.requests.append(request.url) if len(result.requests) < MAX_REQUESTS else None)
    started = time.monotonic()
    page.goto(url, wait_until="load", timeout=45000)
    result.load_ms = int((time.monotonic() - started) * 1000)
    settle(page, settle_seconds, cancel)
    result.title = page.title().strip()
    result.final_url = page.url
    shoot(page, out / "desktop.jpg", full_page=False)
    result.files["desktop"] = "desktop.jpg"

    progress(2, 4, "full page")
    scroll_through(page)
    shoot(page, out / "fullpage.jpg", full_page=True)
    result.files["fullpage"] = "fullpage.jpg"
    desktop.close()

    progress(3, 4, "mobile")
    cancel.check()
    mobile = browser.new_context(viewport=MOBILE, user_agent=MOBILE_AGENT, is_mobile=True, has_touch=True,
                                 device_scale_factor=2, ignore_https_errors=True)
    page = mobile.new_page()
    guard_requests(page, allow_private, blocked)
    page.goto(url, wait_until="load", timeout=45000)
    settle(page, min(settle_seconds, 2.0), cancel)
    shoot(page, out / "mobile.jpg", full_page=False)
    result.files["mobile"] = "mobile.jpg"
    mobile.close()
    return result, blocked
