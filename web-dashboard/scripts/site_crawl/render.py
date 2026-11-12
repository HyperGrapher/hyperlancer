"""HTML -> readable Markdown.

Written to cope with real-world pages: it works on semantic markup, but also on pages built
from nested <div>s (every block-level element becomes its own paragraph, <br><br> separates
paragraphs). Images are handed to a callback so the caller decides where they are saved.
"""

from __future__ import annotations

import re
import textwrap
from pathlib import PurePosixPath
from typing import Callable
from urllib.parse import unquote, urljoin, urlparse

from bs4 import Comment, NavigableString, Tag

from .urls import IMAGE_EXT

BR = "\x00"
_WS = re.compile(r"\s+")

SKIP_TAGS = {
    "script", "style", "noscript", "template", "svg", "button", "select", "option",
    "input", "textarea", "form", "head", "title", "meta", "link", "canvas", "dialog",
}
INLINE_TAGS = {
    "a", "b", "strong", "i", "em", "span", "u", "small", "sup", "sub", "font", "label",
    "abbr", "code", "mark", "cite", "s", "del", "big", "time", "kbd", "q",
}
BLOCK_TAGS = {
    "div", "p", "ul", "ol", "li", "table", "tr", "td", "th", "h1", "h2", "h3", "h4", "h5",
    "h6", "section", "article", "blockquote", "figure", "figcaption", "header", "footer",
    "nav", "aside", "pre", "hr", "form", "dl", "dt", "dd", "main", "center", "details", "summary",
}
LAZY_SRC_ATTRS = ("src", "data-src", "data-lazy-src", "data-original", "data-lazy")

# image_cb(tag) -> (relative_path, alt_text) or None when the image is not exported
ImageCb = Callable[[Tag], "tuple[str, str] | None"]


def collapse(text: str) -> str:
    return _WS.sub(" ", text)


def md_escape_alt(alt: str) -> str:
    return collapse(alt).strip().replace("[", "(").replace("]", ")")


def md_url(url: str) -> str:
    return url.replace(" ", "%20").replace("(", "%28").replace(")", "%29")


def pick_srcset(value: str | None) -> str | None:
    """Largest candidate of a srcset attribute ('a.jpg 480w, b.jpg 800w' -> 'b.jpg')."""
    if not value:
        return None
    best, best_size = None, -1.0
    for candidate in re.split(r",\s+", value.strip()):
        bits = candidate.strip().split()
        if not bits or bits[0].startswith("data:"):
            continue
        size = 1.0
        if len(bits) > 1 and bits[1][-1:].lower() in ("w", "x"):
            try:
                size = float(bits[1][:-1])
            except ValueError:
                pass
        if size > best_size:
            best, best_size = bits[0], size
    return best


class Renderer:
    def __init__(self, base_url: str, image_cb: ImageCb, is_ignored: Callable[[str], bool] = lambda url: False):
        self.base_url = base_url
        self.image_cb = image_cb
        self.is_ignored = is_ignored
        self._links: list[str | None] = []

    # ---------------------------------------------------------------- public

    def render(self, root: Tag) -> list[str]:
        blocks: list[str] = []
        self._blocks(root, blocks)
        return tidy(blocks)

    def img_url(self, tag: Tag) -> str | None:
        """Best available absolute URL of an <img>, preferring the largest srcset candidate."""
        value = pick_srcset(tag.get("srcset") or tag.get("data-srcset"))
        if not value and tag.parent is not None and tag.parent.name == "picture":
            for source in tag.parent.find_all("source"):
                value = pick_srcset(source.get("srcset") or source.get("data-srcset"))
                if value:
                    break
        if not value:
            for attr in LAZY_SRC_ATTRS:
                candidate = (tag.get(attr) or "").strip()
                if candidate and not candidate.startswith("data:"):
                    value = candidate
                    break
        return urljoin(self.base_url, value) if value else None

    def href(self, tag: Tag) -> str | None:
        value = (tag.get("href") or "").strip()
        if not value or value.startswith("#") or value.lower().startswith("javascript"):
            return None
        return md_url(urljoin(self.base_url, value))

    # ---------------------------------------------------------------- blocks

    def _blocks(self, node: Tag, out: list[str]) -> None:
        buf: list[str] = []

        def flush() -> None:
            if buf:
                self._emit_text("".join(buf), out)
                buf.clear()

        for child in node.children:
            if isinstance(child, Comment):
                continue
            if isinstance(child, NavigableString):
                buf.append(collapse(str(child)))
                continue
            if not isinstance(child, Tag):
                continue

            name = child.name
            if name in SKIP_TAGS or self._hidden(child):
                continue
            if name == "br":
                buf.append(BR)
                continue
            if name == "img":
                flush()
                self._image_block(child, out)
                continue
            if name == "picture":
                flush()
                image = child.find("img")
                if image is not None:
                    self._image_block(image, out)
                continue
            if name == "iframe":
                flush()
                src = (child.get("src") or child.get("data-src") or "").strip()
                if src:
                    out.append(f"[Embedded content: {src}]({md_url(urljoin(self.base_url, src))})")
                continue
            if name in ("video", "audio"):
                flush()
                src = (child.get("src") or "").strip()
                if not src:
                    source = child.find("source", src=True)
                    src = (source.get("src") if source else "") or ""
                if src:
                    out.append(f"[{name.capitalize()}: {src}]({md_url(urljoin(self.base_url, src))})")
                continue

            level = self._heading_level(child)
            if level:
                flush()
                text = self._inline_text(child)
                if text:
                    out.append("#" * level + " " + text)
                continue

            if name in ("ul", "ol"):
                flush()
                text = self._list(child)
                if text:
                    out.append(text)
                continue
            if name == "table":
                flush()
                text = self._table(child)
                if text:
                    out.append(text)
                continue
            if name == "pre":
                flush()
                code = child.get_text()
                if code.strip():
                    out.append("```\n" + code.strip("\n") + "\n```")
                continue
            if name == "blockquote":
                flush()
                inner: list[str] = []
                self._blocks(child, inner)
                if inner:
                    out.append(textwrap.indent("\n\n".join(tidy(inner)), "> ", lambda _l: True))
                continue

            if name in INLINE_TAGS and not self._has_block(child):
                buf.append(self._inline_tag(child))
                continue

            # generic container: recurse (remember link target for images inside <a>)
            flush()
            if name == "a":
                self._links.append(self.href(child))
                self._blocks(child, out)
                self._links.pop()
            else:
                self._blocks(child, out)
        flush()

    def _emit_text(self, raw: str, out: list[str]) -> None:
        paragraphs: list[list[str]] = []
        current: list[str] = []
        for segment in raw.split(BR):
            segment = segment.strip()
            if segment:
                current.append(segment)
            elif current:  # blank segment = two consecutive <br> => new paragraph
                paragraphs.append(current)
                current = []
        if current:
            paragraphs.append(current)
        for lines in paragraphs:
            text = "  \n".join(lines)
            if re.match(r"^(#{1,6}\s|[-+*]\s|\d+[.)]\s|>)", text):
                text = "\\" + text
            out.append(text)

    def _image_block(self, tag: Tag, out: list[str]) -> None:
        if not self.img_url(tag):
            return
        result = self.image_cb(tag)
        if not result:
            return
        rel, alt = result
        md = f"![{md_escape_alt(alt)}]({md_url(rel)})"
        link = next((link for link in reversed(self._links) if link), None)
        if link and PurePosixPath(unquote(urlparse(link).path)).suffix.lower() in IMAGE_EXT:
            link = None  # lightbox link to the full-size file: the saved image already is that file
        out.append(f"[{md}]({link})" if link else md)

    # ---------------------------------------------------------------- inline

    def _inline(self, node: Tag) -> str:
        parts: list[str] = []
        for child in node.children:
            if isinstance(child, Comment):
                continue
            if isinstance(child, NavigableString):
                parts.append(collapse(str(child)))
            elif isinstance(child, Tag):
                if child.name in SKIP_TAGS or child.name in ("img", "picture") or self._hidden(child):
                    continue
                if child.name == "br":
                    parts.append(BR)
                else:
                    parts.append(self._inline_tag(child))
        return "".join(parts)

    def _inline_tag(self, tag: Tag) -> str:
        inner = self._inline(tag)
        name = tag.name
        if name in ("b", "strong"):
            return _wrap(inner, "**")
        if name in ("i", "em"):
            return _wrap(inner, "*")
        if name == "code" and inner.strip():
            return f"`{inner.strip()}`"
        if name in ("sup", "sub"):
            return f"<{name}>{inner.strip()}</{name}>" if inner.strip() else inner
        if name == "a":
            href = self.href(tag)
            text = inner.replace(BR, " ")
            core = text.strip()
            if not href or not core:
                return inner
            lead = text[: len(text) - len(text.lstrip())]
            trail = text[len(text.rstrip()):]
            return f"{lead}[{core}]({href}){trail}"
        return inner

    def _inline_text(self, tag: Tag) -> str:
        return collapse(self._inline(tag).replace(BR, " ")).strip()

    # ---------------------------------------------------------------- structures

    @staticmethod
    def _heading_level(tag: Tag) -> int | None:
        if tag.name and re.fullmatch(r"h[1-6]", tag.name):
            return int(tag.name[1])
        return None

    def _list(self, tag: Tag) -> str:
        ordered = tag.name == "ol"
        items: list[str] = []
        for number, li in enumerate(tag.find_all("li", recursive=False), start=1):
            blocks: list[str] = []
            self._blocks(li, blocks)
            blocks = tidy(blocks)
            if not blocks:
                continue
            marker = f"{number}. " if ordered else "- "
            pad = " " * len(marker)
            first, *rest = blocks
            lines = [marker + first.replace("\n", "\n" + pad)]
            lines += [textwrap.indent(block, pad) for block in rest]
            items.append("\n".join(lines))
        return "\n".join(items)

    def _table(self, tag: Tag) -> str:
        rows: list[list[str]] = []
        for tr in tag.find_all("tr"):
            cells = [self._inline_text(c) for c in tr.find_all(["th", "td"], recursive=False)]
            if any(cells):
                rows.append(cells)
        return _md_table(rows)

    # ---------------------------------------------------------------- helpers

    def _has_block(self, tag: Tag) -> bool:
        def is_block(t: Tag) -> bool:
            if t.name in BLOCK_TAGS or t.name in ("iframe", "picture", "video", "audio"):
                return True
            if t.name == "img":
                url = self.img_url(t)
                return bool(url) and not self.is_ignored(url)
            return False

        return tag.find(is_block) is not None

    @staticmethod
    def _hidden(tag: Tag) -> bool:
        style = tag.get("style") or ""
        return (
            bool(re.search(r"display\s*:\s*none", style, re.I))
            or tag.has_attr("hidden")
            or tag.get("aria-hidden") == "true" and tag.name in ("div", "span", "section")
        )


def _wrap(text: str, mark: str) -> str:
    """Wrap each non-empty line of `text` in a Markdown emphasis mark."""
    pieces = text.split(BR)
    wrapped = []
    for piece in pieces:
        core = piece.strip()
        if not core:
            wrapped.append(piece)
            continue
        lead = piece[: len(piece) - len(piece.lstrip())]
        trail = piece[len(piece.rstrip()):]
        wrapped.append(f"{lead}{mark}{core}{mark}{trail}")
    return BR.join(wrapped)


def _md_table(rows: list[list[str]]) -> str:
    if not rows:
        return ""
    width = max(len(r) for r in rows)
    rows = [[c.replace("|", "\\|") for c in r] + [""] * (width - len(r)) for r in rows]
    lines = ["| " + " | ".join(rows[0]) + " |", "| " + " | ".join(["---"] * width) + " |"]
    lines += ["| " + " | ".join(r) + " |" for r in rows[1:]]
    return "\n".join(lines)


def tidy(blocks: list[str]) -> list[str]:
    """Drop empty blocks, consecutive duplicates and headings that introduce nothing."""
    cleaned: list[str] = []
    for block in blocks:
        block = block.strip()
        if block and (not cleaned or cleaned[-1] != block):
            cleaned.append(block)

    def level(block: str) -> int:
        m = re.match(r"^(#{1,6}) ", block)
        return len(m.group(1)) if m else 0

    result: list[str] = []
    for i, block in enumerate(cleaned):
        lvl = level(block)
        if lvl:
            nxt = cleaned[i + 1] if i + 1 < len(cleaned) else None
            if nxt is None or (level(nxt) and level(nxt) <= lvl):
                continue
        result.append(block)
    return result
