"""Finding a browser. We never download one: the user's own Chrome or Edge is used (PRD M3)."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from playwright.sync_api import Playwright


def launch(playwright: Playwright, browser_path: str | None) -> Any:
    options: dict[str, Any] = {"headless": True, "args": ["--no-sandbox"]}
    if browser_path and Path(browser_path).exists():
        return playwright.chromium.launch(executable_path=browser_path, **options)
    last_error: Exception | None = None
    for channel in ("chrome", "msedge"):
        try:
            return playwright.chromium.launch(channel=channel, **options)
        except Exception as exc:  # the channel is simply not installed
            last_error = exc
    try:
        return playwright.chromium.launch(**options)
    except Exception as exc:
        raise RuntimeError("No Chrome or Edge found. Install Google Chrome or Microsoft Edge.") from (last_error or exc)
