"""Crawl settings, read from the job spec (docs/PRD.md §5.11 and §8.3)."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


class OptionsError(ValueError):
    pass


@dataclass
class Options:
    output_dir: Path
    start_urls: list[str]
    max_pages: int = 200
    max_depth: int = 5
    include_subdomains: bool = False
    include: list[str] = field(default_factory=list)
    exclude: list[str] = field(default_factory=list)
    respect_robots: bool = True
    delay: float = 0.5
    concurrency: int = 1
    images: bool = True
    documents: bool = True
    media: bool = False
    fonts: bool = False
    extensions: list[str] | None = None  # when set, only these file extensions are downloaded as files
    max_file_mb: float = 25
    max_total_mb: float = 1024
    main_selector: str | None = None
    strip: list[str] = field(default_factory=list)
    keep_chrome: bool = False
    languages: list[str] = field(default_factory=list)
    wordpress: str = "auto"
    allow_private: bool = False
    timeout: float = 30.0

    @classmethod
    def from_params(cls, params: dict[str, Any], output_dir: Path) -> "Options":
        types = params.get("types") or {}

        def number(key: str, default: float, low: float, high: float) -> float:
            try:
                value = float(params.get(key, default))
            except (TypeError, ValueError):
                raise OptionsError(f"{key} must be a number.")
            if not low <= value <= high:
                raise OptionsError(f"{key} must be between {low:g} and {high:g}.")
            return value

        def strings(key: str) -> list[str]:
            value = params.get(key) or []
            if not isinstance(value, list) or not all(isinstance(v, str) for v in value):
                raise OptionsError(f"{key} must be a list of text values.")
            return [v.strip() for v in value if v.strip()]

        urls = strings("startUrls") or ([params["url"]] if params.get("url") else [])
        if not urls:
            raise OptionsError("startUrls is required.")
        urls = [u if "://" in u else "https://" + u for u in urls]
        for key in ("include", "exclude"):
            for pattern in strings(key):
                try:
                    re.compile(pattern)
                except re.error as error:
                    raise OptionsError(f"Invalid regular expression in {key}: {pattern!r} ({error})")
        wordpress = params.get("wordpress", "auto")
        if wordpress not in ("auto", "off", "only"):
            raise OptionsError('wordpress must be "auto", "off" or "only".')
        extensions = params.get("extensions")
        return cls(
            output_dir=output_dir,
            start_urls=urls,
            max_pages=int(number("maxPages", 200, 1, 20000)),
            max_depth=int(number("maxDepth", 5, 0, 50)),
            include_subdomains=bool(params.get("includeSubdomains", False)),
            include=strings("include"),
            exclude=strings("exclude"),
            respect_robots=bool(params.get("respectRobots", True)),
            delay=number("delay", 0.5, 0, 30),
            concurrency=int(number("concurrency", 1, 1, 4)),
            images=bool(types.get("images", True)),
            documents=bool(types.get("documents", True)),
            media=bool(types.get("media", False)),
            fonts=bool(types.get("fonts", False)),
            extensions=[e.lower() if e.startswith(".") else "." + e.lower() for e in extensions] if extensions else None,
            max_file_mb=number("maxFileMb", 25, 0.1, 2048),
            max_total_mb=number("maxTotalMb", 1024, 0.001, 51200),
            main_selector=(params.get("mainSelector") or None),
            strip=strings("strip"),
            keep_chrome=bool(params.get("keepChrome", False)),
            languages=[l.strip("/").lower() for l in strings("languages")],
            wordpress=wordpress,
            allow_private=bool(params.get("allowPrivate", False)),
            timeout=number("timeout", 30, 2, 300),
        )
