"""Everything read from the HTML of the requested page: metadata, headings, links, contacts, WordPress hints."""

from __future__ import annotations

import json
import re
import urllib.parse
from typing import Any

from bs4 import BeautifulSoup

from .urls import get_host, strip_www

SOCIAL_NETWORKS = {
    "facebook.com": "facebook", "fb.com": "facebook", "instagram.com": "instagram", "twitter.com": "x", "x.com": "x",
    "linkedin.com": "linkedin", "youtube.com": "youtube", "youtu.be": "youtube", "tiktok.com": "tiktok",
    "pinterest.com": "pinterest", "t.me": "telegram", "telegram.me": "telegram", "github.com": "github",
    "behance.net": "behance", "dribbble.com": "dribbble", "vimeo.com": "vimeo", "medium.com": "medium",
}
WHATSAPP_HOSTS = {"wa.me", "api.whatsapp.com", "web.whatsapp.com", "chat.whatsapp.com"}
SOCIAL_SKIP = ("/sharer", "/share", "/intent/", "/dialog/", "share?", "/plugins/")

CONSENT_TOOLS = (
    ("Cookiebot", ("cookiebot.com", "cybotcookiebot")), ("OneTrust", ("onetrust.com", "cdn.cookielaw.org", "optanon")),
    ("CookieYes", ("cookieyes.com", "cky-consent")), ("Complianz", ("complianz", "cmplz")),
    ("Iubenda", ("iubenda.com", "_iub")), ("Osano", ("osano.com",)), ("Termly", ("termly.io",)),
    ("Borlabs Cookie", ("borlabs-cookie", "borlabscookie")), ("Cookie Law Info", ("cookie-law-info", "cli-modal")),
    ("Quantcast Choice", ("quantcast.mgr.consensu.org", "quantcast choice")), ("Usercentrics", ("usercentrics.eu",)),
    ("Klaro", ("klaro.js", "klaro.kiprotect")), ("CookieConsent (Osano OSS)", ("cookieconsent.min.js",)),
)

IGNORED_EMAIL_DOMAINS = ("example.com", "domain.com", "email.com", "sentry.io", "wixpress.com")
EMAIL_RE = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}")
PHONE_RE = re.compile(r"(?<![\w/])(?:\+|00)\d[\d\s().-]{7,17}\d(?!\w)")
ASSET_SUFFIXES = (".png", ".jpg", ".jpeg", ".gif", ".webp", ".svg", ".css", ".js", ".woff", ".woff2")


def empty_page(error: str | None = None, content_type: str | None = None) -> dict[str, Any]:
    return {
        "parseable_html": False, "error": error, "content_type": content_type,
        "title": None, "title_length": 0, "meta_description": None, "meta_description_length": 0,
        "meta_keywords": None, "meta_generator": None, "html_language": None, "declared_charset": None,
        "meta_robots": None, "noindex": False, "canonical_url": None, "favicon_url": None, "viewport": None,
        "open_graph": {"title": None, "description": None, "image": None, "type": None, "url": None, "site_name": None},
        "twitter": {"card": None, "title": None, "description": None, "image": None},
        "hreflang": [], "headings": [], "h1_count": 0,
        "image_count": 0, "images_missing_alt": 0, "form_count": 0, "script_count": 0,
        "external_script_hosts": [], "structured_data_types": [],
        "links": {"internal": 0, "external": 0, "nofollow": 0},
        "consent_tool": None,
    }


def empty_contacts() -> dict[str, Any]:
    return {"emails": [], "phones": [], "address": None, "socials": [], "whatsapp": []}


def empty_wordpress() -> dict[str, Any]:
    return {"detected": False, "version": None, "theme": None, "plugins": []}


def _meta(soup: BeautifulSoup, **attrs: Any) -> str | None:
    tag = soup.find("meta", attrs=attrs)
    content = tag.get("content") if tag else None
    return str(content).strip() if content else None


def _name_meta(soup: BeautifulSoup, name: str) -> str | None:
    tag = soup.find("meta", attrs={"name": re.compile(rf"^{re.escape(name)}$", re.I)})
    return str(tag.get("content", "")).strip() if tag and tag.get("content") is not None else None


def _rels(tag: Any) -> list[str]:
    value = tag.get("rel") or []
    return [item.lower() for item in (value if isinstance(value, list) else str(value).split())]


def _walk_jsonld(value: Any, types: set[str], found: dict[str, Any]) -> None:
    if isinstance(value, dict):
        kinds = value.get("@type")
        if kinds:
            types.update(str(kind) for kind in (kinds if isinstance(kinds, list) else [kinds]))
        for key in ("telephone", "email"):
            if isinstance(value.get(key), str):
                found.setdefault(key, []).append(value[key])
        address = value.get("address")
        if isinstance(address, dict) and not found.get("address"):
            parts = [address.get(k) for k in ("streetAddress", "addressLocality", "addressRegion", "postalCode", "addressCountry")]
            text = ", ".join(str(p if not isinstance(p, dict) else p.get("name", "")) for p in parts if p)
            if text:
                found["address"] = text
        for child in value.values():
            _walk_jsonld(child, types, found)
    elif isinstance(value, list):
        for child in value:
            _walk_jsonld(child, types, found)


def _clean_phone(raw: str) -> str | None:
    digits = re.sub(r"\D", "", raw)
    if not 8 <= len(digits) <= 15:
        return None
    return ("+" if raw.strip().startswith(("+", "00")) else "") + (digits[2:] if raw.strip().startswith("00") else digits)


def _wp_hints(html: str, generator: str | None) -> dict[str, Any]:
    lowered = html.lower()
    detected = any(m in lowered for m in ("/wp-content/", "/wp-includes/", "wp-json"))
    version = None
    if generator:
        match = re.match(r"wordpress\s+([\d.]+)", generator, re.I)
        if match:
            version, detected = match.group(1), True
    theme = re.search(r"/wp-content/themes/([a-z0-9_-]+)/", lowered)
    plugins = sorted(set(re.findall(r"/wp-content/plugins/([a-z0-9_-]+)/", lowered)))
    return {"detected": detected, "version": version, "theme": theme.group(1) if theme else None, "plugins": plugins[:40]}


def extract(html_text: str, final_url: str, content_type: str) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    """Returns (page, contacts, wordpress) for an HTML document."""
    page = empty_page(None, content_type)
    contacts = empty_contacts()
    if not html_text.strip():
        page["error"] = "No HTML body was returned."
        return page, contacts, empty_wordpress()

    soup = BeautifulSoup(html_text, "html.parser")
    host = strip_www(get_host(final_url).lower())
    html_element = soup.find("html")
    title = soup.find("title")
    description = _name_meta(soup, "description")
    generator = _name_meta(soup, "generator")
    robots = _name_meta(soup, "robots")

    charset = None
    charset_tag = soup.find("meta", charset=True)
    if charset_tag:
        charset = charset_tag.get("charset")
    if not charset:
        equiv = soup.find("meta", attrs={"http-equiv": re.compile(r"content-type", re.I)})
        match = re.search(r"charset\s*=\s*([^;\s]+)", str(equiv.get("content", "")), re.I) if equiv else None
        charset = match.group(1) if match else None

    canonical = soup.find("link", rel=lambda v: v and "canonical" in [i.lower() for i in (v if isinstance(v, list) else [v])])
    favicon = None
    for link in soup.find_all("link", href=True):
        rels = _rels(link)
        if "icon" in rels or "shortcut" in rels:
            favicon = urllib.parse.urljoin(final_url, str(link["href"]))
            break
    if favicon is None:
        for link in soup.find_all("link", href=True):
            if "apple-touch-icon" in _rels(link):
                favicon = urllib.parse.urljoin(final_url, str(link["href"]))
                break

    scripts = soup.find_all("script")
    script_hosts = sorted({
        h for h in (get_host(urllib.parse.urljoin(final_url, str(t["src"]))) for t in scripts if t.get("src"))
        if h and h != get_host(final_url)
    })
    images = soup.find_all("img")

    headings = []
    for tag in soup.find_all(re.compile(r"^h[1-6]$")):
        text = tag.get_text(" ", strip=True)
        if text:
            headings.append({"level": int(tag.name[1]), "text": text[:120]})

    hreflang = []
    for link in soup.find_all("link", hreflang=True, href=True):
        if "alternate" in _rels(link):
            hreflang.append({"lang": str(link["hreflang"]), "url": urllib.parse.urljoin(final_url, str(link["href"]))})

    internal = external = nofollow = 0
    emails: list[str] = []
    phones: list[str] = []
    socials: dict[tuple[str, str], None] = {}
    whatsapp: dict[str, None] = {}
    for anchor in soup.find_all("a", href=True):
        href = str(anchor["href"]).strip()
        lowered = href.lower()
        if lowered.startswith("mailto:"):
            emails.append(urllib.parse.unquote(href[7:].split("?", 1)[0]).strip())
            continue
        if lowered.startswith("tel:"):
            phones.append(urllib.parse.unquote(href[4:]).strip())
            continue
        if lowered.startswith(("javascript:", "#", "data:")):
            continue
        absolute = urllib.parse.urljoin(final_url, href)
        target_host = (get_host(absolute) or "").lower()
        if not target_host:
            continue
        if "nofollow" in [r.lower() for r in (anchor.get("rel") or [])]:
            nofollow += 1
        if strip_www(target_host) == host:
            internal += 1
        else:
            external += 1
        if target_host in WHATSAPP_HOSTS or lowered.startswith("whatsapp://"):
            whatsapp[absolute] = None
            continue
        network = next((n for d, n in SOCIAL_NETWORKS.items() if target_host == d or target_host.endswith("." + d)), None)
        if network and not any(skip in lowered for skip in SOCIAL_SKIP):
            socials[(network, absolute.split("#")[0])] = None

    jsonld_types: set[str] = set()
    found: dict[str, Any] = {}
    for script in soup.find_all("script", attrs={"type": re.compile(r"application/ld\+json", re.I)}):
        try:
            _walk_jsonld(json.loads(script.string or script.get_text()), jsonld_types, found)
        except (json.JSONDecodeError, TypeError):
            continue
    emails.extend(found.get("email", []))
    phones.extend(found.get("telephone", []))

    # Counts are taken before the tree is pruned: decomposing <noscript> also invalidates the <img> tags inside it.
    image_count = len(images)
    images_missing_alt = sum(1 for i in images if not i.has_attr("alt"))
    form_count = len(soup.find_all("form"))
    script_count = len(scripts)

    # Visible text (scripts/styles removed) catches footer contact details that are not linked.
    for junk in soup(["script", "style", "noscript", "template"]):
        junk.decompose()
    visible = soup.get_text(" ", strip=True)
    emails.extend(m for m in EMAIL_RE.findall(visible) if not m.lower().endswith(ASSET_SUFFIXES))
    phones.extend(PHONE_RE.findall(visible))

    address = found.get("address")
    if not address:
        tag = soup.find("address")
        text = tag.get_text(" ", strip=True) if tag else ""
        address = text[:300] or None

    clean_emails = list(dict.fromkeys(
        e.lower() for e in emails
        if EMAIL_RE.fullmatch(e) and not any(e.lower().split("@")[1] == d or e.lower().split("@")[1].endswith("." + d) for d in IGNORED_EMAIL_DOMAINS) and not e.lower().endswith(ASSET_SUFFIXES)
    ))[:10]
    clean_phones = list(dict.fromkeys(p for p in (_clean_phone(x) for x in phones) if p))[:10]

    lowered_html = html_text.lower()
    consent = next((name for name, markers in CONSENT_TOOLS if any(m in lowered_html for m in markers)), None)

    og = {key: _meta(soup, property=f"og:{key}") for key in ("title", "description", "image", "type", "url", "site_name")}
    if og["image"]:
        og["image"] = urllib.parse.urljoin(final_url, og["image"])
    twitter = {key: _meta(soup, name=f"twitter:{key}") for key in ("card", "title", "description", "image")}
    if twitter["image"]:
        twitter["image"] = urllib.parse.urljoin(final_url, twitter["image"])

    title_text = title.get_text(" ", strip=True) if title else None
    page.update(
        parseable_html=True, error=None,
        title=title_text, title_length=len(title_text or ""),
        meta_description=description, meta_description_length=len(description or ""),
        meta_keywords=_name_meta(soup, "keywords"), meta_generator=generator,
        html_language=html_element.get("lang") if html_element else None, declared_charset=charset,
        meta_robots=robots, noindex=bool(robots and "noindex" in robots.lower()),
        canonical_url=urllib.parse.urljoin(final_url, str(canonical["href"])) if canonical and canonical.get("href") else None,
        favicon_url=favicon or urllib.parse.urljoin(final_url, "/favicon.ico"),
        viewport=_name_meta(soup, "viewport"),
        open_graph=og, twitter=twitter, hreflang=hreflang[:30], headings=headings[:80],
        h1_count=sum(1 for h in headings if h["level"] == 1),
        image_count=image_count, images_missing_alt=images_missing_alt,
        form_count=form_count, script_count=script_count,
        external_script_hosts=script_hosts[:30], structured_data_types=sorted(jsonld_types)[:30],
        links={"internal": internal, "external": external, "nofollow": nofollow},
        consent_tool=consent,
    )
    contacts.update(
        emails=clean_emails, phones=clean_phones, address=address,
        socials=[{"network": n, "url": u} for (n, u) in socials][:20], whatsapp=list(whatsapp)[:5],
    )
    return page, contacts, _wp_hints(html_text, generator)
