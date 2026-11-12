"""Entry point: `python -m site_profile --spec FILE` (job mode) or `python -m site_profile URL [--output FILE]`."""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Any

from site_tools import exitcodes
from site_tools.cancel import Cancelled, CancelToken
from site_tools.events import Emitter
from site_tools.spec import SpecError, load_spec

from .core import ProfileParams, collect
from .summary import summarize

STATUS_BY_EXIT = {exitcodes.OK: "ok", exitcodes.PARTIAL: "partial", exitcodes.FAILED: "failed", exitcodes.BLOCKED: "failed"}


def write_atomic(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="site_profile", description="Passive website profile (schema v2).")
    parser.add_argument("url", nargs="?", help="Website URL or hostname (not needed with --spec).")
    parser.add_argument("--spec", help="Job spec JSON written by the dashboard worker.")
    parser.add_argument("--output", help="Write the JSON report to this file.")
    parser.add_argument("--events", action="store_true", help="Print NDJSON progress events on stdout.")
    parser.add_argument("--timeout", type=float, default=None, help="Per-request timeout in seconds (default 12).")
    parser.add_argument("--allow-private", action="store_true", help="Allow private, loopback and LAN targets.")
    parser.add_argument("--resolver", help="DNS resolver IP to use instead of the system resolver.")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    params: dict[str, Any] = {}
    cancel_file = None
    output: Path | None = Path(args.output) if args.output else None
    job_mode = bool(args.spec)

    if args.spec:
        try:
            spec = load_spec(args.spec)
        except SpecError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return exitcodes.INVALID_INPUT
        params = spec["params"]
        cancel_file = spec.get("cancelFile")
        if output is None:
            output = Path(params.get("output") or Path(spec.get("dir") or Path(args.spec).parent) / "profile.json")

    url = args.url or params.get("url")
    emit = Emitter(enabled=job_mode or args.events)
    if not url:
        print("error: a URL is required (positional argument or spec params.url).", file=sys.stderr)
        return exitcodes.INVALID_INPUT

    cancel = CancelToken(cancel_file)
    cancel.install_signal_handlers()
    profile_params = ProfileParams(
        url=str(url),
        timeout=float(args.timeout or params.get("timeout") or 12),
        allow_private=bool(args.allow_private or params.get("allowPrivate")),
        resolver=args.resolver or params.get("resolver") or None,
    )

    try:
        report, code = collect(profile_params, emit, cancel)
    except ValueError as exc:  # unusable URL
        emit.log("error", str(exc))
        print(f"error: {exc}", file=sys.stderr)
        return exitcodes.INVALID_INPUT
    except Cancelled:
        emit.log("warn", "Cancelled.")
        return exitcodes.CANCELLED

    if output is not None:
        # The normalized summary travels inside the report, so the app reads one file.
        report["summary"] = summarize(report)
        write_atomic(output, report)
        emit.result(path=str(output), status=report["status"], errors=len(report["errors"]), duration_ms=report["metadata"]["duration_ms"])
    elif not (job_mode or args.events):
        print(json.dumps(report, indent=2, ensure_ascii=False))
    emit.progress(8, 8, "done")
    return code
