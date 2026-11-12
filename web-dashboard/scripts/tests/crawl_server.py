"""A tiny local web server for crawler tests: routes map a path to (status, headers, body) or a callable."""

from __future__ import annotations

import base64
import contextlib
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

PNG_1X1 = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==")


def png(width: int, height: int, filler: int = 0) -> bytes:
    """A PNG header with the given size plus filler bytes (not a valid picture, but enough for sniffing and sizes)."""
    import struct

    return b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" + struct.pack(">II", width, height) + b"\x08\x06\x00\x00\x00" + b"\x00" * 4 + b"\x00" * filler


def html(body: str, title: str = "Page", lang: str = "en", head: str = "") -> tuple[int, dict, bytes]:
    page = f'<!doctype html><html lang="{lang}"><head><meta charset="utf-8"><title>{title}</title>{head}</head><body>{body}</body></html>'
    return 200, {"Content-Type": "text/html; charset=utf-8"}, page.encode()


def js(data) -> tuple[int, dict, bytes]:
    return 200, {"Content-Type": "application/json"}, json.dumps(data).encode()


@contextlib.contextmanager
def serve(routes: dict):
    hits: list[str] = []

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):  # noqa: N802
            parts = urlparse(self.path)
            hits.append(self.path)
            route = routes.get(parts.path)
            if callable(route):
                route = route(parse_qs(parts.query))
            status, headers, body = route or (404, {"Content-Type": "text/plain"}, b"not found")
            self.send_response(status)
            for key, value in {"Content-Length": str(len(body)), **headers}.items():
                self.send_header(key, value)
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):  # silence
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield f"http://127.0.0.1:{server.server_address[1]}", hits
    finally:
        server.shutdown()
        server.server_close()
