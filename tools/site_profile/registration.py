"""Domain registration: RDAP first, WHOIS as the fallback (and the only source for .tr)."""

from __future__ import annotations

import datetime as dt
import json
import urllib.parse
from typing import Any

from . import whois
from .http import fetch

IANA_RDAP_BOOTSTRAP = "https://data.iana.org/rdap/dns.json"

# .tr is not in the IANA RDAP bootstrap registry, so use its registry WHOIS server.
WHOIS_SERVERS = {
    "tr": "whois.trabis.gov.tr",
    "com": "whois.verisign-grs.com",
    "net": "whois.verisign-grs.com",
    "org": "whois.pir.org",
}

RESERVED_TLDS = {"test", "localhost", "local", "example", "invalid", "internal", "lan", "home", "corp", "intranet", "arpa"}

NOTE = "Registry records may omit or redact contact details; only domain-level metadata is collected."


def empty(domain: str | None = None, note: str | None = None) -> dict[str, Any]:
    return {
        "lookup_domain": domain,
        "lookup_method": None,
        "registration_date": None,
        "registration_year": None,
        "domain_age_years": None,
        "expiration_date": None,
        "last_updated": None,
        "registrar": None,
        "nameservers": [],
        "statuses": [],
        "registrant": None,
        "transfer_locked": None,
        "transfer_status": None,
        "rdap_server": None,
        "whois_server": None,
        "source": None,
        "error": None,
        "note": note or NOTE,
    }


def json_get(url: str, timeout: float, allow_private: bool) -> tuple[Any | None, str | None]:
    response = fetch(url, timeout=timeout, max_bytes=5_000_000, allow_private=allow_private)
    if response.error:
        return None, response.error
    if not response.status or response.status >= 400:
        return None, f"HTTP {response.status}"
    try:
        return json.loads(response.body.decode("utf-8")), None
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        return None, f"Invalid JSON response: {exc}"


def rdap_base_for_domain(domain: str, bootstrap: dict[str, Any]) -> tuple[list[str], str | None]:
    matches: list[tuple[int, list[str], str]] = []
    for entry_group, base_urls in bootstrap.get("services", []):
        for entry in entry_group:
            entry = entry.lower().rstrip(".")
            if domain == entry or domain.endswith("." + entry):
                matches.append((len(entry.split(".")), base_urls, entry))
    if not matches:
        return [], None
    _, base_urls, matched_suffix = max(matches, key=lambda item: item[0])
    return sorted(base_urls, key=lambda value: not value.lower().startswith("https://")), matched_suffix


def parse_rdap_date_events(data: dict[str, Any]) -> dict[str, str | None]:
    dates: dict[str, str | None] = {"registration_date": None, "last_updated": None, "expiration_date": None}
    action_keys = {
        "registration": "registration_date", "registered": "registration_date", "creation": "registration_date",
        "last changed": "last_updated", "last update of rdap database": "last_updated",
        "expiration": "expiration_date", "expiry": "expiration_date",
    }
    for event in data.get("events", []):
        target = action_keys.get(str(event.get("eventAction", "")).lower().strip())
        if target and event.get("eventDate") and not dates[target]:
            dates[target] = str(event["eventDate"])
    return dates


def registrar_from_rdap(data: dict[str, Any]) -> str | None:
    for entity in data.get("entities", []):
        if "registrar" not in [str(role).lower() for role in entity.get("roles", [])]:
            continue
        vcard = entity.get("vcardArray")
        if isinstance(vcard, list) and len(vcard) > 1 and isinstance(vcard[1], list):
            for item in vcard[1]:
                if len(item) >= 4 and item[0] in {"fn", "org"}:
                    value = item[3]
                    return " ".join(str(part) for part in value) if isinstance(value, list) else str(value)
        if entity.get("handle"):
            return str(entity["handle"])
    return None


def registrant_from_rdap(data: dict[str, Any]) -> str | None:
    for entity in data.get("entities", []):
        if "registrant" not in [str(role).lower() for role in entity.get("roles", [])]:
            continue
        vcard = entity.get("vcardArray")
        if isinstance(vcard, list) and len(vcard) > 1 and isinstance(vcard[1], list):
            for item in vcard[1]:
                if len(item) >= 4 and item[0] in {"org", "fn"} and isinstance(item[3], (str, list)):
                    value = " ".join(map(str, item[3])) if isinstance(item[3], list) else item[3]
                    if value.strip() and not whois.HIDDEN.search(value):
                        return value.strip()
    return None


def profile_registration(domain_data: dict[str, Any], timeout: float, allow_private: bool = False) -> dict[str, Any]:
    domain = domain_data.get("registrable_domain")
    if domain_data.get("is_ip_address") or not domain:
        return empty(None, "Domain registration details do not apply to a literal IP address.")

    suffix = (domain_data.get("public_suffix") or "").split(".")[-1].lower()
    if not suffix or suffix in RESERVED_TLDS:
        # Local development names (.test, .local, hosts-file entries) have no registry record to look up.
        return empty(domain, "Reserved or local domain name: there is no registry record to look up.")

    result = empty(domain)
    bootstrap, bootstrap_error = json_get(IANA_RDAP_BOOTSTRAP, timeout, allow_private)
    rdap_error = bootstrap_error
    bases: list[str] = []
    matched_suffix = None
    if isinstance(bootstrap, dict):
        bases, matched_suffix = rdap_base_for_domain(domain, bootstrap)

    rdap_data: dict[str, Any] | None = None
    if bases:
        encoded = urllib.parse.quote(domain.encode("idna").decode("ascii"), safe=".-")
        for base in bases:
            url = urllib.parse.urljoin(base.rstrip("/") + "/", "domain/" + encoded)
            candidate, error = json_get(url, timeout, allow_private)
            if isinstance(candidate, dict) and candidate.get("objectClassName") == "domain":
                rdap_data = candidate
                result.update(rdap_server=base, source=url, lookup_method="RDAP")
                break
            rdap_error = error or "RDAP response did not contain a domain object"
    elif not bootstrap_error:
        rdap_error = f"No IANA RDAP bootstrap entry matched .{matched_suffix or domain.split('.')[-1]}"

    if rdap_data:
        result.update(parse_rdap_date_events(rdap_data))
        result["registrar"] = registrar_from_rdap(rdap_data)
        result["nameservers"] = sorted({
            str(item.get("ldhName", "")).lower().rstrip(".")
            for item in rdap_data.get("nameservers", []) if item.get("ldhName")
        })
        result["statuses"] = sorted(set(map(str, rdap_data.get("status", []))))
        result["whois_server"] = rdap_data.get("port43")
        result["registrant"] = registrant_from_rdap(rdap_data)

    # WHOIS fills in what RDAP did not provide (registrar and expiry are the usual gaps).
    if not (result["registration_date"] and result["expiration_date"] and result["registrar"] and result["registrant"]):
        suffix = domain_data.get("public_suffix") or domain.split(".")[-1]
        server = result.get("whois_server") or WHOIS_SERVERS.get(suffix.split(".")[-1].lower())
        if server:
            try:
                parsed = whois.parse_whois_record(whois.whois_query(domain, str(server), timeout))
                if any(parsed[key] for key in ("registration_date", "expiration_date", "registrar", "nameservers")):
                    if str(server).endswith("trabis.gov.tr"):
                        parsed["last_updated"] = None  # TRABIS only reports its own database timestamp
                    for key in ("registration_date", "expiration_date", "last_updated", "registrar", "registrant", "transfer_status"):
                        if parsed.get(key) and not result.get(key):
                            result[key] = parsed[key]
                    for key in ("nameservers", "statuses"):
                        result[key] = sorted(set(result.get(key, [])) | set(parsed.get(key, [])))
                    result["lookup_method"] = "RDAP + WHOIS" if result.get("rdap_server") else "WHOIS"
                    result["whois_server"] = str(server)
                    result["source"] = result["source"] or f"whois://{server}:43"
                elif not result["source"]:
                    result["error"] = f"WHOIS returned no parseable domain metadata; RDAP: {rdap_error}"
            except Exception as exc:  # noqa: BLE001
                if not result["source"]:
                    result["error"] = f"WHOIS lookup failed ({type(exc).__name__}: {exc}); RDAP: {rdap_error}"
        elif not result["source"]:
            result["error"] = rdap_error or "No RDAP service or known WHOIS fallback is available for this suffix."

    result["transfer_locked"] = whois.transfer_locked(result["statuses"], result.get("transfer_status"))

    created = result.get("registration_date")
    if created:
        try:
            birth = dt.date.fromisoformat(str(created)[:10])
            result["registration_date"] = birth.isoformat()
            result["registration_year"] = birth.year
            result["domain_age_years"] = round((dt.datetime.now(dt.timezone.utc).date() - birth).days / 365.2425, 1)
        except ValueError:
            result["registration_year"] = str(created)[:4]
    for key in ("expiration_date", "last_updated"):
        if result.get(key):
            result[key] = whois.parse_date(str(result[key])) or result[key]
    return result
