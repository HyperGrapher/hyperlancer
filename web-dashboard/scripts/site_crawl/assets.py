"""Images and files: downloaded once, de-duplicated by SHA-256, named after the original file."""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Any
from urllib.parse import unquote, urldefrag, urlparse

from .net import Fetcher
from .options import Options
from .urls import DOCUMENT_EXT, FONT_EXT, IMAGE_EXT, MEDIA_EXT, extension, safe_filename

EXT_BY_TYPE = {
    "image/jpeg": ".jpg", "image/png": ".png", "image/gif": ".gif", "image/webp": ".webp", "image/svg+xml": ".svg",
    "image/avif": ".avif", "image/x-icon": ".ico", "image/vnd.microsoft.icon": ".ico", "image/bmp": ".bmp",
    "application/pdf": ".pdf", "application/zip": ".zip", "text/csv": ".csv",
}


def image_size(data: bytes) -> tuple[int, int] | None:
    """Width and height from the file header (PNG, GIF, JPEG, WebP); no image library needed."""
    try:
        if data[:8] == b"\x89PNG\r\n\x1a\n":
            return struct.unpack(">II", data[16:24])
        if data[:6] in (b"GIF87a", b"GIF89a"):
            return struct.unpack("<HH", data[6:10])
        if data[:4] == b"RIFF" and data[8:12] == b"WEBP":
            chunk = data[12:16]
            if chunk == b"VP8 ":
                return struct.unpack("<HH", data[26:30])[0] & 0x3FFF, struct.unpack("<HH", data[26:30])[1] & 0x3FFF
            if chunk == b"VP8L":
                bits = struct.unpack("<I", data[21:25])[0]
                return (bits & 0x3FFF) + 1, ((bits >> 14) & 0x3FFF) + 1
            if chunk == b"VP8X":
                return int.from_bytes(data[24:27], "little") + 1, int.from_bytes(data[27:30], "little") + 1
        if data[:2] == b"\xff\xd8":
            i = 2
            while i + 9 < len(data):
                if data[i] != 0xFF:
                    i += 1
                    continue
                marker = data[i + 1]
                if marker in (0xD8, 0x01) or 0xD0 <= marker <= 0xD7:
                    i += 2
                    continue
                length = struct.unpack(">H", data[i + 2 : i + 4])[0]
                if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
                    height, width = struct.unpack(">HH", data[i + 5 : i + 9])
                    return width, height
                i += 2 + length
    except (struct.error, IndexError):
        return None
    return None


@dataclass
class Asset:
    id: str
    kind: str  # image | file | media
    url: str
    file: str  # relative to the crawl folder, POSIX
    sha256: str
    bytes: int
    mime: str
    width: int | None = None
    height: int | None = None
    alt_texts: list[str] = field(default_factory=list)
    used_on: list[str] = field(default_factory=list)
    aliases: list[str] = field(default_factory=list)  # other URLs that returned identical bytes

    def to_json(self) -> dict[str, Any]:
        return {
            "id": self.id, "kind": self.kind, "url": self.url, "file": self.file, "sha256": self.sha256, "bytes": self.bytes,
            "mime": self.mime, "width": self.width, "height": self.height, "alt_texts": self.alt_texts, "used_on": self.used_on,
            "aliases": self.aliases,
        }


class AssetStore:
    def __init__(self, opts: Options, fetcher: Fetcher, ignore) -> None:  # ignore: Callable[[str], bool]
        self.opts = opts
        self.fetcher = fetcher
        self.ignore = ignore
        self.assets: list[Asset] = []
        self.failures: list[dict[str, str]] = []
        self.total_bytes = 0
        self.cap_reached = False  # set when the total size cap stops further downloads; the crawl then ends gracefully
        self._by_url: dict[str, Asset | None] = {}
        self._by_hash: dict[str, Asset] = {}
        self._names: dict[str, set[str]] = {"images": set(), "files": set()}

    # ------------------------------------------------------------------ policy

    def wants(self, kind: str, url: str) -> bool:
        ext = extension(url)
        if kind == "image":
            return self.opts.images
        if ext in MEDIA_EXT:
            return self.opts.media
        if ext in FONT_EXT:
            return self.opts.fonts
        if self.opts.extensions is not None:
            return ext in self.opts.extensions
        return self.opts.documents and (ext in DOCUMENT_EXT or ext == "")

    # ------------------------------------------------------------------ saving

    def save(self, candidates: list[str], kind: str, page_id: str, alt: str = "") -> Asset | None:
        """Download the first candidate URL that works. Returns the asset (possibly shared with other pages)."""
        reason = "no candidate URL"
        for raw in candidates:
            url = urldefrag(raw)[0]
            if self.ignore(url):
                return None
            if not self.wants(kind, url):
                return None
            if url in self._by_url:
                asset = self._by_url[url]
                if asset:
                    self._use(asset, page_id, alt)
                    return asset
                continue
            asset, reason = self._download(url, kind)
            self._by_url[url] = asset
            if asset:
                self._use(asset, page_id, alt)
                return asset
        self.failures.append({"page": page_id, "url": urldefrag(candidates[0])[0] if candidates else "", "reason": reason})
        return None

    def _use(self, asset: Asset, page_id: str, alt: str) -> None:
        if page_id not in asset.used_on:
            asset.used_on.append(page_id)
        if alt and alt not in asset.alt_texts:
            asset.alt_texts.append(alt)

    def _download(self, url: str, kind: str) -> tuple[Asset | None, str]:
        cap_total = int(self.opts.max_total_mb * 1024 * 1024)
        if self.total_bytes >= cap_total:
            self.cap_reached = True
            return None, "total size limit reached"
        response = self.fetcher.get(url, max_bytes=int(self.opts.max_file_mb * 1024 * 1024))
        if response.blocked:
            return None, "blocked by the network policy"
        if response.error:
            return None, response.error
        if response.too_large:
            return None, f"larger than the {self.opts.max_file_mb:g} MB file limit"
        if response.status != 200:
            return None, f"HTTP {response.status}"
        ctype = response.content_type
        ext = extension(url)
        if kind == "image":
            if not (ctype.startswith("image/") or (ctype == "application/octet-stream" and ext in IMAGE_EXT)):
                return None, f"not an image (server returned {ctype or 'unknown type'})"
        elif "html" in ctype:
            return None, "the server returned a web page instead of the file"
        if not response.body:
            return None, "empty response"
        if self.total_bytes + len(response.body) > cap_total:
            self.cap_reached = True
            return None, "total size limit reached"

        digest = hashlib.sha256(response.body).hexdigest()
        existing = self._by_hash.get(digest)
        if existing:
            existing.aliases.append(url)
            return existing, ""

        is_media = extension(url) in MEDIA_EXT
        folder = "images" if kind == "image" else "files"
        name = safe_filename(PurePosixPath(unquote(urlparse(url).path)).name or kind)
        if not PurePosixPath(name).suffix and ctype in EXT_BY_TYPE:
            name += EXT_BY_TYPE[ctype]
        name = self._unique(folder, name)
        path = self.opts.output_dir / folder / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(response.body)
        size = image_size(response.body) if kind == "image" else None
        asset = Asset(
            id=f"a{len(self.assets) + 1}", kind="media" if is_media else kind, url=url, file=f"{folder}/{name}", sha256=digest,
            bytes=len(response.body), mime=ctype or "application/octet-stream",
            width=size[0] if size else None, height=size[1] if size else None,
        )
        self.assets.append(asset)
        self._by_hash[digest] = asset
        self.total_bytes += len(response.body)
        return asset, ""

    def _unique(self, folder: str, name: str) -> str:
        taken = {n.lower() for n in self._names[folder]}
        stem, dot, ext = name.rpartition(".")
        if not dot:
            stem, ext = name, ""
        candidate, counter = name, 2
        while candidate.lower() in taken:
            candidate = f"{stem}-{counter}" + (f".{ext}" if ext else "")
            counter += 1
        self._names[folder].add(candidate)
        return candidate
