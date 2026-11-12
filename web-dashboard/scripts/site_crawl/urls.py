"""URL helpers: normalisation, link classification and file-system names."""

from __future__ import annotations

import hashlib
import re
import unicodedata
from pathlib import PurePosixPath
from urllib.parse import parse_qsl, unquote, urldefrag, urlencode, urljoin, urlparse

IMAGE_EXT = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".svg", ".bmp", ".ico", ".avif"}
DOCUMENT_EXT = {".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx", ".zip", ".rar", ".7z", ".csv", ".txt", ".rtf", ".odt", ".ods"}
MEDIA_EXT = {".mp4", ".webm", ".mov", ".mp3", ".wav", ".ogg", ".m4a"}
FONT_EXT = {".woff", ".woff2", ".ttf", ".eot", ".otf"}
# Not pages: never queued for the HTML crawl.
ASSET_EXT = IMAGE_EXT | DOCUMENT_EXT | MEDIA_EXT | FONT_EXT | {".css", ".js", ".mjs", ".json", ".xml", ".rss", ".atom"}

_TRACKING = re.compile(r"^(utm_.*|fbclid|gclid|dclid|msclkid|mc_cid|mc_eid|igshid|_ga|_gl)$", re.I)
_PAGE_EXT = re.compile(r"\.(php\d?|html?|shtml|aspx?|jsp|cfm)$", re.I)
_BAD_NAME = re.compile(r'[<>:"/\\|?*\x00-\x1f]')
_RESERVED = {"con", "prn", "aux", "nul", *(f"com{i}" for i in range(1, 10)), *(f"lpt{i}" for i in range(1, 10))}
_FOLD = str.maketrans({"ı": "i", "İ": "I", "ß": "ss", "ø": "o", "Ø": "O", "æ": "ae", "Æ": "AE", "đ": "d", "Đ": "D", "ł": "l", "Ł": "L", "œ": "oe", "Œ": "OE", "þ": "th"})


def fold(text: str) -> str:
    """Strip accents so names are safe on every file system (ç -> c, ı -> i)."""
    return unicodedata.normalize("NFKD", text.translate(_FOLD)).encode("ascii", "ignore").decode()


def clean_url(url: str) -> str:
    """Drop the fragment and tracking parameters."""
    url = urldefrag(url)[0]
    parts = urlparse(url)
    query = [(k, v) for k, v in parse_qsl(parts.query, keep_blank_values=True) if not _TRACKING.match(k)]
    return parts._replace(query=urlencode(query)).geturl()


def base_host(url: str) -> str:
    return (urlparse(url).hostname or "").lower().removeprefix("www.")


def key_of(url: str) -> str:
    """Identity of a page for duplicate detection (ignores scheme, www, fragment, trailing slash)."""
    parts = urlparse(url)
    path = unquote(parts.path).rstrip("/") or "/"
    return f"{base_host(url)}{path}" + (f"?{parts.query}" if parts.query else "")


def host_allowed(url: str, base: str, include_subdomains: bool) -> bool:
    host = base_host(url)
    return host == base or (include_subdomains and host.endswith("." + base))


def extension(url: str) -> str:
    return PurePosixPath(unquote(urlparse(url).path)).suffix.lower()


def classify_link(href: str, base_url: str) -> tuple[str, str] | None:
    """("page" | "file" | "external-scheme", absolute URL), or None for links that are not fetchable."""
    href = (href or "").strip()
    if not href or href.startswith(("#", "javascript:", "mailto:", "tel:", "data:", "sms:", "whatsapp:")):
        return None
    absolute = urljoin(base_url, href)
    if urlparse(absolute).scheme not in ("http", "https"):
        return None
    ext = extension(absolute)
    return ("file" if ext in ASSET_EXT else "page"), clean_url(absolute)


def safe_filename(name: str, limit: int = 100) -> str:
    """A name that is valid on Windows and POSIX: no reserved characters or device names, bounded length."""
    name = _BAD_NAME.sub("_", unquote(name)).strip(" .") or "file"
    stem, dot, ext = name.rpartition(".")
    if not dot:
        stem, ext = name, ""
    if stem.lower() in _RESERVED:
        stem = "_" + stem
    ext = ("." + ext) if ext else ""
    if len(stem) + len(ext) > limit:
        stem = stem[: max(8, limit - len(ext) - 9)] + "-" + hashlib.sha1(name.encode("utf-8")).hexdigest()[:8]
    return stem + ext


def page_slug(path: str, query: str = "") -> str:
    """'/products/olive-oil.php' -> 'products--olive-oil'; at most 100 characters, a hash replaces the overflow."""
    path = _PAGE_EXT.sub("", unquote(path).strip("/"))
    parts = [re.sub(r"[^A-Za-z0-9._-]+", "-", fold(segment)).strip("-.").lower() for segment in path.split("/")]
    slug = "--".join(p for p in parts if p) or "index"
    if query:
        slug += "--q-" + hashlib.sha1(query.encode("utf-8")).hexdigest()[:6]
    if len(slug) > 100:
        slug = slug[:90] + "-" + hashlib.sha1(slug.encode("utf-8")).hexdigest()[:8]
    if slug in _RESERVED:
        slug = "_" + slug
    return slug


def split_lang(path: str, languages: list[str]) -> tuple[str | None, str]:
    """('/en/about', ['en']) -> ('en', '/about'). (None, path) when no prefix matches."""
    segments = path.strip("/").split("/")
    if segments and segments[0].lower() in languages:
        return segments[0].lower(), "/" + "/".join(segments[1:])
    return None, path
