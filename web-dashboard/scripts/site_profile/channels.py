"""Contact channels: which way to reach the business, from the homepage and its contact-like pages.

Phone numbers are normalised to E.164 and classified; WhatsApp numbers come from chat links and
are `confirmed`, a mobile number without such a link is only `likely` (numbers are never checked
against WhatsApp). Emails are labelled generic or personal. People come from schema.org Person.
"""

from __future__ import annotations

import json
import re
import urllib.parse
from typing import Any

from bs4 import BeautifulSoup

from .urls import get_host, strip_www

CONTACT_PATHS = ("iletisim", "iletişim", "bize-ulasin", "bize-ulaşın", "contact", "contact-us", "kontakt", "hakkimizda",
                 "hakkımızda", "about", "about-us", "ulasim", "ulaşım", "kunyemiz", "impressum")
MAX_CONTACT_PAGES = 5
GENERIC_LOCALS = {
    "info", "iletisim", "iletişim", "contact", "hello", "hi", "mail", "email", "office", "ofis", "sales", "satis", "satış",
    "support", "destek", "bilgi", "admin", "service", "servis", "randevu", "reservation", "rezervasyon", "kontakt",
    "enquiries", "inquiry", "marketing", "pazarlama", "muhasebe", "accounting", "hr", "ik", "kariyer", "career", "careers",
    "export", "ihracat", "ithalat", "import", "siparis", "sipariş", "order", "orders", "webmaster", "postmaster", "noreply",
}
WIDGETS = (
    ("Joinchat", ("joinchat", "join-chat")),
    ("Click to Chat", ("click-to-chat", "ht-ctc", "wa-chat-box", "ctc-")),
    ("GetButton", ("getbutton.io", "gb-widget")),
    ("Elfsight", ("elfsight-app", "apps.elfsight.com")),
    ("WhatsApp Chat widget", ("whatsapp-chat-widget", "wa-widget", "whatsapp-button")),
)
REGION_BY_SUFFIX = {"tr": "TR", "de": "DE", "uk": "GB", "fr": "FR", "it": "IT", "es": "ES", "nl": "NL", "az": "AZ", "gr": "GR",
                    "bg": "BG", "ro": "RO", "us": "US", "ae": "AE", "sa": "SA", "ru": "RU", "ir": "IR", "pl": "PL"}


def default_region(host: str) -> str | None:
    return REGION_BY_SUFFIX.get(host.rsplit(".", 1)[-1].lower())


def classify_phone(raw: str, region: str | None) -> dict[str, Any] | None:
    """E.164 form and line type of a number as written on a page; None when it is not a valid number."""
    # Imported here so a missing package (an old "uv sync") only costs the phone numbers, not the whole profile.
    import phonenumbers
    from phonenumbers import PhoneNumberType

    type_names = {
        PhoneNumberType.MOBILE: "mobile",
        PhoneNumberType.FIXED_LINE: "fixed",
        PhoneNumberType.FIXED_LINE_OR_MOBILE: "fixed or mobile",
        PhoneNumberType.TOLL_FREE: "toll free",
        PhoneNumberType.PREMIUM_RATE: "premium rate",
        PhoneNumberType.VOIP: "voip",
    }
    text = raw.strip()
    if text.startswith("00"):
        text = "+" + text[2:]
    try:
        number = phonenumbers.parse(text, None if text.startswith("+") else region)
    except phonenumbers.NumberParseException:
        return None
    if not phonenumbers.is_valid_number(number):
        return None
    return {"e164": phonenumbers.format_number(number, phonenumbers.PhoneNumberFormat.E164),
            "display": phonenumbers.format_number(number, phonenumbers.PhoneNumberFormat.INTERNATIONAL),
            "type": type_names.get(phonenumbers.number_type(number), "other"),
            "country": phonenumbers.region_code_for_number(number)}


def parse_whatsapp_link(url: str) -> dict[str, str] | None:
    """Number and prefilled text of a WhatsApp chat link (wa.me, api./web.whatsapp.com/send, whatsapp://send)."""
    parts = urllib.parse.urlsplit(url)
    query = urllib.parse.parse_qs(parts.query)
    number = ""
    if parts.scheme == "whatsapp" or (parts.hostname or "").lower() in ("api.whatsapp.com", "web.whatsapp.com"):
        number = (query.get("phone") or [""])[0]
    elif (parts.hostname or "").lower() == "wa.me":
        number = parts.path.strip("/")
    number = re.sub(r"\D", "", number)
    if not number:
        return None
    return {"number": "+" + number, "text": (query.get("text") or [""])[0]}


def classify_email(address: str) -> str:
    local = address.split("@")[0].lower()
    return "generic" if local in GENERIC_LOCALS or re.fullmatch(r"(info|iletisim|contact|bilgi)\d*", local) else "personal"


def widgets_in(html: str) -> list[str]:
    lowered = html.lower()
    return [name for name, markers in WIDGETS if any(m in lowered for m in markers)]


def people_in(soup: BeautifulSoup) -> list[dict[str, str]]:
    found: list[dict[str, str]] = []

    def walk(value: Any) -> None:
        if isinstance(value, list):
            for child in value:
                walk(child)
        elif isinstance(value, dict):
            types = value.get("@type")
            types = types if isinstance(types, list) else [types]
            if "Person" in types and isinstance(value.get("name"), str):
                found.append({"name": value["name"].strip()[:80], "role": str(value.get("jobTitle") or "").strip()[:80]})
            for child in value.values():
                walk(child)

    for script in soup.find_all("script", attrs={"type": re.compile(r"application/ld\+json", re.I)}):
        try:
            walk(json.loads(script.string or script.get_text()))
        except (ValueError, TypeError):
            continue
    return found


def contact_page_urls(soup: BeautifulSoup, base_url: str, limit: int = MAX_CONTACT_PAGES) -> list[str]:
    """Contact-like pages of the same site, found from the homepage's links (never guessed)."""
    host = strip_www((get_host(base_url) or "").lower())
    found: dict[str, None] = {}
    for anchor in soup.find_all("a", href=True):
        absolute = urllib.parse.urljoin(base_url, str(anchor["href"]).strip()).split("#")[0]
        parts = urllib.parse.urlsplit(absolute)
        if parts.scheme not in ("http", "https") or strip_www((parts.hostname or "").lower()) != host:
            continue
        path = urllib.parse.unquote(parts.path).strip("/").lower()
        if path and path.split("/")[-1] in CONTACT_PATHS and absolute != base_url.split("#")[0]:
            found[absolute] = None
        if len(found) >= limit:
            break
    return list(found)


def build(pages: list[dict[str, Any]], host: str) -> dict[str, Any]:
    """Merge what each scanned page found. A page is {url, emails, phones, whatsapp, widgets, people}."""
    region = default_region(host)
    phones: dict[str, dict[str, Any]] = {}
    whatsapp: dict[str, dict[str, Any]] = {}
    emails: dict[str, dict[str, Any]] = {}
    people: dict[tuple[str, str], dict[str, str]] = {}
    widgets: list[str] = []
    for page in pages:
        for raw in page.get("phones", []):
            info = classify_phone(raw, region)
            if info:
                phones.setdefault(info["e164"], {**info, "pages": []})["pages"].append(page["url"])
        for link in page.get("whatsapp", []):
            parsed = parse_whatsapp_link(link)
            if parsed:
                entry = whatsapp.setdefault(parsed["number"], {"number": parsed["number"], "text": parsed["text"], "status": "confirmed", "pages": []})
                entry["pages"].append(page["url"])
        for address in page.get("emails", []):
            emails.setdefault(address, {"address": address, "kind": classify_email(address), "pages": []})["pages"].append(page["url"])
        for person in page.get("people", []):
            people.setdefault((person["name"], person["role"]), {**person, "page": page["url"]})
        widgets += [w for w in page.get("widgets", []) if w not in widgets]
    # A mobile number without a chat link may well be on WhatsApp; it is a candidate, nothing more.
    for e164, info in phones.items():
        if info["type"] in ("mobile", "fixed or mobile") and e164 not in whatsapp:
            whatsapp[e164] = {"number": e164, "text": "", "status": "likely", "pages": list(info["pages"])}
    for entry in [*phones.values(), *whatsapp.values(), *emails.values()]:
        entry["pages"] = list(dict.fromkeys(entry["pages"]))
    return {
        "phones": list(phones.values())[:15], "whatsapp": list(whatsapp.values())[:10], "widgets": widgets,
        "emails": list(emails.values())[:20], "people": list(people.values())[:15],
        "pagesScanned": [p["url"] for p in pages],
    }


def scan_html(html: str, url: str, contacts: dict[str, Any]) -> dict[str, Any]:
    """What one page offers: the basic contacts the page parser found plus widgets and people."""
    soup = BeautifulSoup(html, "html.parser")
    return {
        "url": url, "emails": contacts.get("emails", []), "phones": contacts.get("phones", []),
        "whatsapp": contacts.get("whatsapp", []), "widgets": widgets_in(html), "people": people_in(soup),
    }


def empty() -> dict[str, Any]:
    return {"phones": [], "whatsapp": [], "widgets": [], "emails": [], "people": [], "pagesScanned": []}
