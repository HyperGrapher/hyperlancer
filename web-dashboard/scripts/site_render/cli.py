"""Entry point: `python -m site_render --spec FILE` or `python -m site_render URL --out DIR`."""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Any

from playwright.sync_api import sync_playwright

from site_profile import trackers
from site_tools import exitcodes, netguard
from site_tools.cancel import Cancelled, CancelToken
from site_tools.events import Emitter
from site_tools.spec import SpecError, load_spec

from .browser import launch
from .capture import capture_site

DEFAULT_SETTLE_SECONDS = 6.0


def write_atomic(path: Path, data: dict[str, Any]) -> None:
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="site_render", description="Screenshots and rendered requests of a page.")
    parser.add_argument("url", nargs="?")
    parser.add_argument("--spec")
    parser.add_argument("--out")
    parser.add_argument("--browser", help="Path of a Chrome or Edge executable.")
    parser.add_argument("--allow-private", action="store_true")
    args = parser.parse_args(argv)

    params: dict[str, Any] = {}
    cancel_file = None
    out = Path(args.out) if args.out else None
    if args.spec:
        try:
            spec = load_spec(args.spec)
        except SpecError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return exitcodes.INVALID_INPUT
        params, cancel_file = spec["params"], spec.get("cancelFile")
        out = out or Path(params.get("outputDir") or Path(spec.get("dir") or Path(args.spec).parent) / "render")
    url = args.url or params.get("url")
    if not url or out is None:
        print("error: a URL and an output folder (or a spec) are required.", file=sys.stderr)
        return exitcodes.INVALID_INPUT
    allow_private = bool(args.allow_private or params.get("allowPrivate"))
    emit = Emitter(enabled=bool(args.spec))

    try:
        netguard.check_url(str(url), allow_private=allow_private)
    except netguard.BlockedTarget as exc:
        emit.log("error", str(exc))
        print(f"error: {exc}", file=sys.stderr)
        return exitcodes.BLOCKED

    out.mkdir(parents=True, exist_ok=True)
    cancel = CancelToken(cancel_file)
    cancel.install_signal_handlers()
    try:
        with sync_playwright() as playwright:
            browser = launch(playwright, args.browser or params.get("browserPath"))
            try:
                captured, blocked = capture_site(
                    browser, str(url), out, float(params.get("settleSeconds") or DEFAULT_SETTLE_SECONDS),
                    allow_private, cancel, emit.progress)
            finally:
                browser.close()
    except Cancelled:
        return exitcodes.CANCELLED
    except Exception as exc:
        message = str(exc).splitlines()[0] if str(exc) else exc.__class__.__name__
        emit.log("error", message)
        print(f"error: {message}", file=sys.stderr)
        return exitcodes.FAILED

    report = {
        "url": str(url), "finalUrl": captured.final_url, "title": captured.title, "loadMs": captured.load_ms,
        "requestCount": len(captured.requests), "blockedRequests": len(blocked),
        "trackers": trackers.detect_requests(captured.requests), "files": captured.files,
    }
    write_atomic(out / "render.json", report)
    emit.result(path=str(out / "render.json"), status="ok", requests=len(captured.requests))
    emit.progress(4, 4, "done")
    return exitcodes.OK
