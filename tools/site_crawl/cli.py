"""`python -m site_crawl --spec FILE` (job mode) or `python -m site_crawl URL --out DIR [--max-pages N]` for a quick try."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

from site_tools import exitcodes
from site_tools.cancel import Cancelled, CancelToken
from site_tools.events import Emitter
from site_tools.spec import SpecError, load_spec

from .crawler import Crawl
from .options import Options, OptionsError


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="site_crawl", description="Export a website as Markdown, images and files.")
    parser.add_argument("url", nargs="?")
    parser.add_argument("--spec", help="Job spec JSON written by the dashboard worker.")
    parser.add_argument("--out", help="Output folder.")
    parser.add_argument("--max-pages", type=int)
    parser.add_argument("--allow-private", action="store_true")
    parser.add_argument("--events", action="store_true", help="Print NDJSON events on stdout.")
    args = parser.parse_args(argv)

    params: dict[str, Any] = {}
    cancel_file = None
    out: Path | None = Path(args.out) if args.out else None
    if args.spec:
        try:
            spec = load_spec(args.spec)
        except SpecError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return exitcodes.INVALID_INPUT
        params, cancel_file = spec["params"], spec.get("cancelFile")
        out = out or Path(params.get("outputDir") or Path(spec.get("dir") or Path(args.spec).parent) / "crawl")
    if args.url:
        params["startUrls"] = [args.url]
    if args.max_pages:
        params["maxPages"] = args.max_pages
    if args.allow_private:
        params["allowPrivate"] = True
    if out is None:
        print("error: --out (or a spec) is required.", file=sys.stderr)
        return exitcodes.INVALID_INPUT

    emit = Emitter(enabled=bool(args.spec) or args.events)
    try:
        opts = Options.from_params(params, out)
    except OptionsError as exc:
        emit.log("error", str(exc))
        print(f"error: {exc}", file=sys.stderr)
        return exitcodes.INVALID_INPUT

    cancel = CancelToken(cancel_file)
    cancel.install_signal_handlers()
    crawl = Crawl(opts, emit, cancel)
    try:
        code = crawl.run()
    except Cancelled:
        return exitcodes.CANCELLED
    manifest = crawl.manifest
    emit.result(path=str(out / "manifest.json"), status=manifest["status"], pages=manifest["counts"]["pages"],
                images=manifest["counts"]["images"], files=manifest["counts"]["files"], bytes=manifest["counts"]["bytes"],
                errors=manifest["counts"]["errors"], stop_reason=manifest.get("stop_reason"))
    if not (args.spec or args.events):
        print(f"{manifest['status']}: {manifest['counts']['pages']} pages, {manifest['counts']['images']} images -> {out}")
    return code
