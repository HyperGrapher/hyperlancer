"""Normalized summary of a profile report (PRD decision 2: the profiler emits it itself).

The desktop app stores this next to the raw report and reads only this shape, so a change
to the raw report is absorbed here. Every key is always present; unknown values are None or
empty. ``facts`` holds the columns the app copies onto the ``sites`` row for lists and filters.

This is a port of the old PHP ``ProfileNormalizer`` (schema v2 only).
"""

from __future__ import annotations

import re
from typing import Any

SUMMARY_VERSION = 2

SECURITY_HEADERS = [
    "Strict-Transport-Security", "Content-Security-Policy", "X-Frame-Options",
    "X-Content-Type-Options", "Referrer-Policy", "Permissions-Policy",
]


def _get(data: Any, path: str, default: Any = None) -> Any:
    for key in path.split("."):
        if not isinstance(data, dict) or key not in data:
            return default
        data = data[key]
    return default if data is None else data


def _int(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        return int(value)
    if isinstance(value, str):
        try:
            return int(float(value))
        except ValueError:
            return None
    return None


def _float(value: Any) -> float | None:
    if isinstance(value, bool) or value is None:
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _list(value: Any) -> list[Any]:
    return list(value) if isinstance(value, (list, tuple)) else []


def _dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def _day(value: Any) -> str | None:
    return value[:10] if isinstance(value, str) and value else None


def _technologies(raw: dict[str, Any]) -> list[dict[str, Any]]:
    out = []
    for item in _list(raw.get("technologies")):
        if not isinstance(item, dict) or not item.get("name"):
            continue
        confidence = item.get("confidence")
        out.append({
            "category": str(item.get("category") or "Other"),
            "name": str(item["name"]),
            "version": str(item["version"]) if item.get("version") is not None else None,
            "confidence": confidence if confidence in ("low", "medium", "high") else "low",
            "evidence": [str(e) for e in _list(item.get("evidence"))],
        })
    return out


def _first(tech: list[dict[str, Any]], category: str) -> dict[str, Any] | None:
    return next((t for t in tech if t["category"] == category), None)


def _cms(tech: list[dict[str, Any]], wordpress: dict[str, Any], parseable: bool) -> dict[str, Any]:
    cms = _first(tech, "CMS")
    if cms:
        version = cms["version"] or (wordpress.get("version") if cms["name"] == "WordPress" else None)
        return {"name": cms["name"], "version": version}
    # A page that loaded without a known CMS is hand-built or unrecognised.
    return {"name": "Custom" if parseable else None, "version": None}


def _mx(records: list[Any]) -> list[dict[str, Any]]:
    out = []
    for record in records:
        match = re.match(r"^(\d+)\s+(\S+)$", str(record))
        if match:
            out.append({"pref": int(match.group(1)), "host": match.group(2).lower().rstrip(".")})
    return sorted(out, key=lambda r: (r["pref"], r["host"]))


def _issuer(issuer: Any) -> str | None:
    if not isinstance(issuer, str) or not issuer:
        return None
    for field in ("organizationName", "commonName"):
        match = re.search(field + r"=([^,]+)", issuer)
        if match:
            return match.group(1).strip()
    return issuer


def _sitemaps(raw: dict[str, Any]) -> dict[str, Any]:
    robots = _dict(_get(raw, "robots_and_sitemaps.robots"))
    sitemaps = _dict(_get(raw, "robots_and_sitemaps.sitemaps"))
    return {
        "robotsStatus": _int(robots.get("status")),
        "disallowRules": _int(robots.get("disallow_rule_count")) or 0,
        "allowRules": _int(robots.get("allow_rule_count")) or 0,
        "crawlDelay": robots.get("crawl_delay"),
        "files": [
            {"url": str(f.get("url") or ""), "kind": str(f.get("kind") or ""), "urls": f.get("urls"),
             "error": f.get("error"), "warning": f.get("warning")}
            for f in _list(sitemaps.get("files")) if isinstance(f, dict)
        ],
        "totalUrls": _int(sitemaps.get("total_urls")) or 0,
        "urls": [
            {"loc": str(u.get("loc") or ""), "lastmod": u.get("lastmod")}
            for u in _list(sitemaps.get("urls")) if isinstance(u, dict)
        ],
        "truncated": bool(sitemaps.get("truncated")),
    }


def _hosting_label(edge: str | None, org: str | None) -> str | None:
    if edge:
        return edge
    if not org:
        return None
    org = re.sub(r",\s*[A-Z]{2}$", "", org)
    if " - " in org:
        org = org.split(" - ", 1)[1]
    return org.strip() or None


def summarize(raw: dict[str, Any]) -> dict[str, Any]:
    status = raw.get("status")
    outcome = status if status in ("ok", "partial", "failed") else "failed"
    errors = [
        {"section": str(e.get("section") or ""), "code": str(e.get("code") or ""), "message": str(e.get("message") or "")}
        for e in _list(raw.get("errors")) if isinstance(e, dict)
    ]

    tech = _technologies(raw)
    host_records = _dict(_get(raw, "dns.website_host_records.records"))
    domain_records = _dict(_get(raw, "dns.domain_records.records"))
    wordpress = _dict(raw.get("wordpress"))
    page = _dict(raw.get("page"))
    parseable = bool(page.get("parseable_html"))

    cms = _cms(tech, wordpress, parseable)
    language = _first(tech, "Likely backend language")
    server_hint = _first(tech, "Server-side hint")
    language_version = None
    if language and server_hint and str(server_hint["name"]).lower().startswith(str(language["name"]).lower()):
        match = re.search(r"/([\d.]+)", str(server_hint["name"]))
        language_version = match.group(1) if match else None
    http_server = _first(tech, "HTTP server header")
    edge = _first(tech, "Hosting / edge")

    ips = [str(ip) for ip in _list(_get(raw, "hosting.ips")) or _list(host_records.get("A"))]
    ipv6 = [str(ip) for ip in _list(_get(raw, "hosting.ipv6")) or _list(host_records.get("AAAA"))]
    as_name = _get(raw, "hosting.as_name")

    reg = _dict(raw.get("domain_registration"))
    ns_values = [str(v) for v in _list(_get(raw, "dns.ns.values"))]
    ns_source = _get(raw, "dns.ns.source")
    if not ns_values and reg.get("nameservers"):
        ns_values = [str(v) for v in _list(reg.get("nameservers"))]
        ns_source = "registry"

    tls = _dict(raw.get("tls_certificate"))
    security = _dict(_get(raw, "http.security_headers.present"))
    present = [name for name, value in security.items() if value not in (None, "")]
    missing = _get(raw, "http.security_headers.missing")
    missing = list(missing) if isinstance(missing, list) else [h for h in SECURITY_HEADERS if h not in present]

    og = _dict(page.get("open_graph"))
    headings = [
        {"level": _int(h.get("level")) or 0, "text": str(h.get("text") or "")}
        for h in _list(page.get("headings")) if isinstance(h, dict)
    ]
    meta_robots = page.get("meta_robots")

    summary: dict[str, Any] = {
        "summaryVersion": SUMMARY_VERSION,
        "schemaVersion": _int(raw.get("schema_version")) or 2,
        "outcome": outcome,
        "errors": errors,
        "url": {
            "requested": _get(raw, "input.requested_url"),
            "final": _get(raw, "http.final_url"),
            "redirects": [
                {"status": _int(r.get("status")) or 0, "from": str(r.get("from") or ""), "to": str(r.get("to") or "")}
                for r in _list(_get(raw, "http.redirects")) if isinstance(r, dict)
            ],
        },
        "http": {
            "status": _int(_get(raw, "http.status")),
            "ttfbMs": _int(_get(raw, "http.ttfb_ms")),
            "totalMs": _int(_get(raw, "http.total_ms")),
            "htmlBytes": _int(_get(raw, "http.html_bytes")),
            "compression": _get(raw, "http.compression"),
            "httpVersion": _get(raw, "http.http_version"),
            "httpsRedirect": _get(raw, "http.checks.https_redirect"),
            "wwwCanonical": _get(raw, "http.checks.www_canonical"),
            "wwwRedirect": _get(raw, "http.checks.www_redirect"),
            "tlsVerified": _get(raw, "http.checks.tls_verified"),
            "blocked": bool(_get(raw, "http.bot_protection.detected", False)),
            "blockedBy": _get(raw, "http.bot_protection.provider"),
            "error": _get(raw, "http.error"),
        },
        "tech": tech,
        "cms": {
            "name": cms["name"],
            "version": cms["version"],
            "theme": wordpress.get("theme"),
            "plugins": _list(wordpress.get("plugins")),
        },
        "hosting": {
            "ips": ips,
            "ipv6": ipv6,
            "org": as_name,
            "asn": _get(raw, "hosting.asn"),
            "prefix": _get(raw, "hosting.prefix"),
            "country": _get(raw, "hosting.country"),
            "reverseDns": _get(raw, "hosting.reverse_dns"),
            "edge": edge["name"] if edge else None,
            "server": http_server["name"] if http_server else _get(raw, "http.notable_headers.server"),
            "language": language["name"] if language else None,
            "languageVersion": language_version,
        },
        "dns": {
            "ns": ns_values,
            "nsSource": ns_source,
            "mx": _mx(_list(domain_records.get("MX"))),
            "emailProvider": _get(raw, "email.provider"),
            "spf": (_list(_get(raw, "dns.domain_records.spf_records")) or [None])[0],
            "dmarc": (_list(_get(raw, "dns.domain_records.dmarc_records")) or [None])[0],
            "dnssec": bool(_get(raw, "dns.domain_records.dnskey_present", False)),
            "caa": [str(v) for v in _list(domain_records.get("CAA"))],
            "txt": [str(v) for v in _list(domain_records.get("TXT"))],
        },
        "emailAuth": _dict(raw.get("email_auth")),
        "trackers": _dict(raw.get("trackers")),
        "tls": {
            "verified": bool(tls.get("connection_verified")),
            "issuer": _issuer(tls.get("issuer")),
            "validFrom": tls.get("valid_from_utc"),
            "validUntil": tls.get("valid_until_utc"),
            "daysLeft": _float(tls.get("days_until_expiry")),
            "sanCount": _int(tls.get("dns_subject_alt_name_count")) or 0,
            "sans": _list(tls.get("dns_subject_alt_names_sample")),
            "fingerprint": tls.get("sha256_fingerprint"),
            "error": tls.get("error"),
        },
        "domain": {
            "registrar": reg.get("registrar"),
            "registrant": reg.get("registrant"),
            "created": reg.get("registration_date"),
            "expires": reg.get("expiration_date"),
            "updated": reg.get("last_updated"),
            "ageYears": _float(reg.get("domain_age_years")),
            "statuses": _list(reg.get("statuses")),
            "transferLocked": reg.get("transfer_locked"),
            "method": reg.get("lookup_method"),
            "source": reg.get("source"),
        },
        "seo": {
            "title": page.get("title"),
            "titleLength": _int(page.get("title_length")) or 0,
            "description": page.get("meta_description"),
            "descriptionLength": _int(page.get("meta_description_length")) or 0,
            "canonical": page.get("canonical_url"),
            "robots": meta_robots,
            "noindex": bool(page.get("noindex", "noindex" in str(meta_robots or "").lower())),
            "lang": page.get("html_language"),
            "hreflang": _list(page.get("hreflang")),
            "viewport": page.get("viewport"),
            "ogTitle": og.get("title"),
            "ogImage": og.get("image"),
            "og": og,
            "twitter": page.get("twitter") if isinstance(page.get("twitter"), dict) else {},
            "structuredData": _list(page.get("structured_data_types")),
            "h1Count": _int(page.get("h1_count")) or 0,
            "headings": headings,
            "imagesTotal": _int(page.get("image_count")) or 0,
            "imagesMissingAlt": _int(page.get("images_missing_alt")) or 0,
            "links": page.get("links"),
            "consentTool": page.get("consent_tool"),
            "favicon": page.get("favicon_url"),
            "parseable": parseable,
        },
        "sitemaps": _sitemaps(raw),
        "contact": {
            "emails": _list(_get(raw, "contacts.emails")),
            "phones": _list(_get(raw, "contacts.phones")),
            "address": _get(raw, "contacts.address"),
            "socials": _list(_get(raw, "contacts.socials")),
            "whatsapp": _list(_get(raw, "contacts.whatsapp")),
            "channels": _dict(raw.get("contact_channels")),
        },
        "security": {
            "present": present,
            "missing": missing,
            "cookies": [
                {"name": str(c.get("name") or ""), "secure": bool(c.get("secure")), "httpOnly": bool(c.get("http_only")),
                 "sameSite": c.get("same_site")}
                for c in _list(_get(raw, "http.cookies")) if isinstance(c, dict)
            ],
        },
        "archive": {
            "first": _day(_get(raw, "website_history.earliest_capture_utc")),
            "firstUrl": _get(raw, "website_history.earliest_capture_archive_url"),
            "last": _day(_get(raw, "website_history.latest_capture_utc")),
            "lastUrl": _get(raw, "website_history.latest_capture_archive_url"),
        },
        "design": {
            "estimatedFrom": _get(raw, "design.estimated_from"),
            "latestAsset": _get(raw, "design.latest_asset"),
            "confidence": _get(raw, "design.confidence"),
            "basis": _list(_get(raw, "design.basis")),
            "clues": [
                {"kind": str(c.get("kind") or ""), "label": str(c.get("label") or ""), "date": str(c.get("date") or ""),
                 "detail": c.get("detail"), "strength": str(c.get("strength") or "weak")}
                for c in _list(_get(raw, "design.clues")) if isinstance(c, dict)
            ],
        },
        "limitations": _list(raw.get("limitations")),
    }
    summary["facts"] = facts(summary)
    return summary


def facts(summary: dict[str, Any]) -> dict[str, Any]:
    """Columns copied onto the sites row (the old PHP ``ProfileNormalizer::facts``)."""
    tls_until = summary["tls"]["validUntil"]
    hosting = summary["hosting"]
    return {
        "http_status": summary["http"]["status"],
        "final_url": summary["url"]["final"],
        "title": summary["seo"]["title"],
        "cms": summary["cms"]["name"],
        "cms_version": summary["cms"]["version"],
        "server": hosting["server"],
        "hosting": _hosting_label(hosting["edge"], hosting["org"]),
        "backend_language": hosting["language"],
        "ip_address": (hosting["ips"] or hosting["ipv6"] or [None])[0],
        "email_provider": summary["dns"]["emailProvider"],
        "tls_valid_until": _day(tls_until),
        "domain_expires_on": _day(summary["domain"]["expires"]),
        "registrar": summary["domain"]["registrar"],
        "favicon_url": summary["seo"]["favicon"],
    }
