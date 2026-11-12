"""Orchestrates the profile: runs the sections, collects errors and timings, decides the overall status."""

from __future__ import annotations

import datetime as dt
import re
import time
import urllib.parse
from concurrent.futures import FIRST_COMPLETED, Future, ThreadPoolExecutor, wait
from dataclasses import dataclass
from typing import Any, Callable

from site_tools import exitcodes
from site_tools.cancel import CancelToken, Cancelled
from site_tools.events import Emitter
from site_tools.netguard import BlockedTarget, check_url

from . import SCHEMA_VERSION, TOOL_VERSION, archive, design, dnsinfo, hosting, page as page_mod, registration, sitemaps, tech, tls
from .http import Response, decode_body, fetch, fetch_once
from .urls import domain_parts, get_host, normalize_url, strip_www

MAX_PAGE_BYTES = 2_000_000
TOTAL_PHASES = 8  # resolving, http, page, dns, tls, registration, sitemaps, archive

LIMITATIONS = [
    "Wayback dates cover the homepage host variants queried and show when the archive first saw that page, not the confirmed site build or launch date.",
    "Framework, CMS, server, and language detections are heuristic and include their evidence and confidence.",
    "A website's backend language usually cannot be proven from its public response; no evidence means unknown, not absent.",
    "Public registration services may omit or redact records. Some country-code registries do not offer RDAP or WHOIS over TCP.",
    "Only the supplied page, robots.txt, and up to ten sitemap files are fetched; linked site pages and assets are not crawled.",
]

BOT_MARKERS = (
    ("Cloudflare", ("just a moment...", "cf-chl", "attention required! | cloudflare", "cf-mitigated", "challenge-platform")),
    ("Imperva Incapsula", ("_incapsula_resource", "incapsula incident")),
    ("Sucuri", ("sucuri website firewall", "access denied - sucuri")),
    ("DataDome", ("datadome", "dd_captcha")),
    ("PerimeterX", ("px-captcha", "perimeterx")),
    ("Akamai", ("akamai bot manager", "reference #18.")),
)


@dataclass
class ProfileParams:
    url: str
    timeout: float = 12.0
    allow_private: bool = False
    resolver: str | None = None


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def parse_cookies(set_cookies: list[str]) -> list[dict[str, Any]]:
    parsed: list[dict[str, Any]] = []
    for raw in set_cookies:
        first = raw.split(";", 1)[0]
        name = first.split("=", 1)[0].strip() if "=" in first else first.strip()
        lowered = raw.lower()
        same_site = re.search(r"samesite\s*=\s*([^;]+)", raw, re.I)
        parsed.append({
            "name": name,
            "secure": "; secure" in lowered or lowered.endswith(";secure"),
            "http_only": "; httponly" in lowered or lowered.endswith(";httponly"),
            "same_site": same_site.group(1).strip() if same_site else None,
        })
    return parsed


def notable_headers(headers: dict[str, str]) -> dict[str, str]:
    useful = {
        "server", "x-powered-by", "via", "content-type", "content-length", "content-encoding", "last-modified",
        "etag", "cache-control", "expires", "age", "vary", "x-cache", "cf-cache-status", "cf-ray",
        "x-vercel-id", "x-nf-request-id", "x-served-by", "x-request-id", "x-runtime",
        "x-aspnet-version", "x-generator", "link", "alt-svc", "set-cookie",
    }
    return {key: value for key, value in headers.items() if key in useful}


def security_header_profile(headers: dict[str, str], is_https: bool) -> dict[str, Any]:
    names = {
        "strict-transport-security": "Strict-Transport-Security",
        "content-security-policy": "Content-Security-Policy",
        "x-frame-options": "X-Frame-Options",
        "x-content-type-options": "X-Content-Type-Options",
        "referrer-policy": "Referrer-Policy",
        "permissions-policy": "Permissions-Policy",
    }
    present = {label: headers.get(name) for name, label in names.items()}
    missing = [label for name, label in names.items() if (name != "strict-transport-security" or is_https) and not headers.get(name)]
    return {"present": present, "missing": missing}


def detect_bot_protection(response: Response, body_text: str) -> dict[str, Any]:
    result: dict[str, Any] = {"detected": False, "provider": None, "evidence": []}
    if response.status not in (403, 429, 503) and "cf-mitigated" not in response.headers:
        return result
    haystack = (body_text[:20000] + "\n" + "\n".join(f"{k}: {v}" for k, v in response.headers.items())).lower()
    for provider, markers in BOT_MARKERS:
        found = [m for m in markers if m in haystack]
        if found:
            return {"detected": True, "provider": provider, "evidence": found[:3]}
    if response.headers.get("server", "").lower() == "cloudflare" and response.status in (403, 503):
        return {"detected": True, "provider": "Cloudflare", "evidence": ["server: cloudflare with HTTP " + str(response.status)]}
    return result


def empty_http(url: str) -> dict[str, Any]:
    return {
        "status": None, "final_url": url, "content_type": None, "content_encoding": None, "compression": None,
        "http_version": None, "body_truncated": False, "html_bytes": 0, "ttfb_ms": None, "total_ms": None,
        "redirects": [], "error": None, "error_code": None, "notable_headers": {},
        "security_headers": {"present": {}, "missing": []}, "cookies": [],
        "checks": {"tls_verified": None, "tls_error": None, "https_redirect": None, "www_canonical": None, "www_redirect": None},
        "bot_protection": {"detected": False, "provider": None, "evidence": []},
    }


def http_checks(final_url: str, parts: dict[str, Any], timeout: float, allow_private: bool) -> dict[str, Any]:
    """HTTP to HTTPS redirect and www/apex canonicalisation. Never raises; unknown stays None."""
    host = get_host(final_url).lower()
    checks: dict[str, Any] = {"https_redirect": None, "www_canonical": None, "www_redirect": None}
    options = {"timeout": min(timeout, 6), "max_bytes": 1, "allow_private": allow_private, "verify": False, "follow_redirects": False}

    def location_host(response: Response) -> tuple[str, str] | None:
        if response.status in (301, 302, 303, 307, 308) and response.headers.get("location"):
            absolute = urllib.parse.urljoin(response.url, response.headers["location"])
            split = urllib.parse.urlsplit(absolute)
            return split.scheme, (split.hostname or "").lower()
        return None

    if not parts.get("is_ip_address"):
        plain = fetch_once(f"http://{host}/", **options)
        if plain.ok:
            target = location_host(plain)
            checks["https_redirect"] = bool(target and target[0] == "https")
        registrable = (parts.get("registrable_domain") or "").lower()
        if host.startswith("www.") and strip_www(host) == registrable:
            checks["www_canonical"], alt = "www", registrable
        elif host == registrable:
            checks["www_canonical"], alt = "apex", f"www.{registrable}"
        else:
            alt = None
        if alt:
            other = fetch_once(f"https://{alt}/", **options)
            if other.ok:
                target = location_host(other)
                checks["www_redirect"] = bool(target and target[1] == host)
    return checks


def _section(name: str, fn: Callable[[], Any], timings: dict[str, int], default: Any, errors: list[dict[str, str]]) -> Any:
    started = time.perf_counter()
    try:
        return fn()
    except Cancelled:
        raise
    except Exception as exc:  # noqa: BLE001 - a broken section must not sink the profile
        errors.append({"section": name, "code": "exception", "message": f"{type(exc).__name__}: {exc}"})
        return default
    finally:
        timings[name] = timings.get(name, 0) + round((time.perf_counter() - started) * 1000)


def _wait(futures: dict[Future, str], cancel: CancelToken, on_done: Callable[[str], None]) -> None:
    pending = set(futures)
    while pending:
        cancel.check()
        done, pending = wait(pending, timeout=0.4, return_when=FIRST_COMPLETED)
        for future in done:
            on_done(futures[future])


def collect(params: ProfileParams, emit: Emitter, cancel: CancelToken) -> tuple[dict[str, Any], int]:
    """Builds the schema v2 report. Returns (report, exit_code). Raises ValueError for unusable input."""
    started = time.perf_counter()
    timings: dict[str, int] = {}
    errors: list[dict[str, str]] = []
    progress = {"done": 0}

    def phase_done(label: str) -> None:
        progress["done"] += 1
        emit.progress(progress["done"], TOTAL_PHASES, label)

    requested_url = normalize_url(params.url)  # ValueError bubbles up as "invalid input"
    initial_host = get_host(requested_url)
    parts = domain_parts(initial_host)
    registrable = parts.get("registrable_domain") or parts["host"]
    timeout = params.timeout

    report: dict[str, Any] = {
        "schema_version": SCHEMA_VERSION, "status": "failed", "errors": errors, "timings_ms": timings,
        "metadata": {
            "tool": "Freelance Website Profiler", "version": TOOL_VERSION, "scanned_at_utc": utc_now(), "duration_ms": 0,
            "method": "Non-invasive public checks: the requested page, DNS, TLS, RDAP/WHOIS, robots/sitemap, and Wayback index.",
        },
        "input": {
            "requested_url": requested_url, "initial_host": initial_host, "final_url": requested_url,
            "registrable_domain": parts.get("registrable_domain"), "site_domain": parts.get("site_domain"),
            "public_suffix": parts.get("public_suffix"), "uses_private_public_suffix": parts.get("uses_private_suffix", False),
            "allow_private_network": params.allow_private,
        },
        "http": empty_http(requested_url), "page": page_mod.empty_page("Not fetched."), "contacts": page_mod.empty_contacts(),
        "wordpress": page_mod.empty_wordpress(),
        "domain_registration": registration.empty(registrable), "website_history": archive.empty(),
        "dns": {"domain_records": dnsinfo.empty_records(registrable), "website_host_records": dnsinfo.empty_records(initial_host),
                "ns": {"values": [], "source": None}},
        "email": {"provider": None, "mx": []},
        "hosting": {"ips": [], "ipv6": [], "reverse_dns": None, "asn": None, "as_name": None, "prefix": None, "country": None, "source": None},
        "tls_certificate": tls.empty("Not checked."),
        "robots_and_sitemaps": sitemaps.empty(), "design": design.empty(), "technologies": [], "limitations": LIMITATIONS,
    }

    def finish(status: str, code: int) -> tuple[dict[str, Any], int]:
        report["status"] = status
        report["metadata"]["duration_ms"] = round((time.perf_counter() - started) * 1000)
        return report, code

    # Phase: resolving (policy check on the initial URL).
    cancel.check()
    emit.progress(0, TOTAL_PHASES, "resolving")
    try:
        check_url(requested_url, params.allow_private)
    except BlockedTarget as blocked:
        errors.append({"section": "http", "code": "blocked", "message": str(blocked)})
        report["http"].update(error=str(blocked), error_code="blocked")
        return finish("failed", exitcodes.BLOCKED)
    phase_done("resolving")

    # Phase: http (everything else depends on its final URL).
    response: Response = _section(
        "http",
        lambda: fetch(requested_url, timeout=timeout, max_bytes=MAX_PAGE_BYTES, allow_private=params.allow_private),
        timings, Response(url=requested_url, error="Internal error", error_code="other"), errors,
    )
    cancel.check()
    phase_done("http")
    final_url = response.url
    final_host = get_host(final_url)
    content_type = response.headers.get("content-type", "")
    body_text = decode_body(response.body, content_type)
    encoding = response.headers.get("content-encoding")
    cookies = parse_cookies(response.set_cookies)
    headers = dict(response.headers)
    if response.set_cookies:
        headers["set-cookie"] = f"[redacted: {len(response.set_cookies)} cookie(s)]"
    bot = detect_bot_protection(response, body_text)
    http = report["http"]
    http.update(
        status=response.status, final_url=final_url, content_type=content_type or None, content_encoding=encoding,
        compression=encoding.lower() if encoding else None, http_version=response.http_version,
        body_truncated=response.truncated, html_bytes=len(response.body), ttfb_ms=response.ttfb_ms,
        total_ms=response.total_ms, redirects=response.redirects, error=response.error, error_code=response.error_code,
        notable_headers=notable_headers(headers),
        security_headers=security_header_profile(headers, final_url.startswith("https://")), cookies=cookies,
        bot_protection=bot,
    )
    http["checks"].update(tls_verified=response.tls_verified, tls_error=response.tls_error)
    report["input"]["final_url"] = final_url
    if response.error:
        errors.append({"section": "http", "code": response.error_code or "error", "message": response.error})
    elif bot["detected"]:
        errors.append({"section": "http", "code": "bot_protection", "message": f"Blocked by bot protection ({bot['provider']}); the profile may be incomplete."})
    elif response.status and response.status >= 400:
        errors.append({"section": "http", "code": "http_status", "message": f"The site answered HTTP {response.status}."})
    if response.tls_verified is False:
        errors.append({"section": "tls", "code": "unverified", "message": f"Certificate could not be verified: {response.tls_error}"})

    # Phase: page.
    is_html = bool(response.body) and ("html" in content_type.lower() or "<html" in body_text[:2000].lower() or "<!doctype html" in body_text[:2000].lower())
    if is_html:
        page, contacts, wordpress = _section("page", lambda: page_mod.extract(body_text, final_url, content_type), timings,
                                              (page_mod.empty_page("Page parsing failed."), page_mod.empty_contacts(), page_mod.empty_wordpress()), errors)
    else:
        message = response.error or "The response does not appear to be an HTML page."
        page, contacts, wordpress = page_mod.empty_page(message, content_type or None), page_mod.empty_contacts(), page_mod.empty_wordpress()
        if response.ok and not bot["detected"] and response.status and response.status < 400:
            errors.append({"section": "page", "code": "not_html", "message": message})
    report.update(page=page, contacts=contacts, wordpress=wordpress)
    phase_done("page")

    # Parallel phases. Each task returns a dict merged into the report by the main thread.
    site_ok = bool(response.status and response.status < 500)
    secure = final_url.startswith("https://")
    split = urllib.parse.urlsplit(final_url)

    def task_dns() -> dict[str, Any]:
        local: list[dict[str, str]] = []
        domain_records = _section("dns", lambda: dnsinfo.profile_dns(registrable, timeout, params.resolver), timings,
                                  dnsinfo.empty_records(registrable), local)
        if final_host == registrable:
            host_records = domain_records
        else:
            host_records = _section("dns", lambda: dnsinfo.profile_dns(final_host, timeout, params.resolver), timings,
                                    dnsinfo.empty_records(final_host), local)
        host_info = _section("hosting", lambda: hosting.profile_hosting(host_records, final_host, timeout, params.resolver), timings,
                             report["hosting"], local)
        return {"domain_records": domain_records, "host_records": host_records, "hosting": host_info, "errors": local}

    def task_tls() -> dict[str, Any]:
        if not secure:
            data = tls.empty("The final page URL uses HTTP, not HTTPS.")
            return {"data": data, "errors": []}
        local: list[dict[str, str]] = []
        data = _section("tls", lambda: tls.profile_tls(final_host, split.port or 443, timeout), timings, tls.empty("TLS check failed."), local)
        return {"data": data, "errors": local}

    def task_registration() -> dict[str, Any]:
        local: list[dict[str, str]] = []
        data = _section("registration", lambda: registration.profile_registration(parts, timeout, params.allow_private), timings,
                        registration.empty(registrable), local)
        return {"data": data, "errors": local}

    def task_sitemaps() -> dict[str, Any]:
        local: list[dict[str, str]] = []
        if not site_ok:
            data = sitemaps.empty()
            data["robots"]["error"] = "Skipped because the website request failed."
            return {"data": data, "errors": []}

        def run() -> dict[str, Any]:
            section, errs = sitemaps.profile_sitemaps(final_url, timeout, params.allow_private)
            local.extend(errs)
            return section

        return {"data": _section("sitemaps", run, timings, sitemaps.empty(), local), "errors": local}

    def task_archive() -> dict[str, Any]:
        local: list[dict[str, str]] = []
        data = _section("archive", lambda: archive.profile_archive([initial_host, final_host]), timings, archive.empty("Archive lookup failed."), local)
        return {"data": data, "errors": local}

    def task_checks() -> dict[str, Any]:
        if not site_ok:
            return {"data": {}, "errors": []}
        local: list[dict[str, str]] = []
        data = _section("http_checks", lambda: http_checks(final_url, parts, timeout, params.allow_private), timings, {}, local)
        return {"data": data, "errors": local}

    def task_design() -> dict[str, Any]:
        local: list[dict[str, str]] = []
        if not is_html:
            return {"data": design.empty(), "errors": []}
        data = _section("design", lambda: design.collect(body_text, final_url, timeout, params.allow_private), timings, design.empty(), local)
        return {"data": data, "errors": local}

    tasks: dict[str, Callable[[], dict[str, Any]]] = {
        "design": task_design, "dns": task_dns, "tls": task_tls, "registration": task_registration,
        "sitemaps": task_sitemaps, "archive": task_archive, "checks": task_checks,
    }
    results: dict[str, dict[str, Any]] = {}
    pool = ThreadPoolExecutor(max_workers=len(tasks))
    try:
        futures = {pool.submit(fn): name for name, fn in tasks.items()}

        def on_done(name: str) -> None:
            if name not in {"checks", "design"}:
                phase_done(name)

        done_futures = dict(futures)
        _wait(done_futures, cancel, on_done)
        for future, name in futures.items():
            results[name] = future.result()
    finally:
        pool.shutdown(wait=False, cancel_futures=True)

    for name in ("design", "dns", "tls", "registration", "sitemaps", "archive", "checks"):
        errors.extend(results[name].get("errors", []))

    dns_result = results["dns"]
    domain_records, host_records = dns_result["domain_records"], dns_result["host_records"]
    reg = results["registration"]["data"]

    ns_values, ns_source = list(domain_records["records"].get("NS", [])), "dns"
    if not ns_values:
        ns_values, ns_source = list(reg.get("nameservers", [])), ("registry" if reg.get("nameservers") else None)
    report["dns"] = {"domain_records": domain_records, "website_host_records": host_records,
                     "ns": {"values": ns_values, "source": ns_source}}
    report["hosting"] = dns_result["hosting"]
    report["email"] = hosting.profile_email(domain_records)
    report["tls_certificate"] = results["tls"]["data"]
    report["domain_registration"] = reg
    report["robots_and_sitemaps"] = results["sitemaps"]["data"]
    report["website_history"] = results["archive"]["data"]
    report["design"] = design.add_context(
        results["design"]["data"],
        report["website_history"].get("earliest_capture_utc"),
        reg.get("registration_date"),
        [u.get("lastmod") or "" for u in report["robots_and_sitemaps"]["sitemaps"]["urls"]],
    )
    http["checks"].update(results["checks"]["data"])

    http_reached = response.error is None and bool(response.status)
    if domain_records.get("nxdomain") and not http_reached:
        errors.append({"section": "dns", "code": "nxdomain", "message": f"{registrable} does not exist in DNS (NXDOMAIN)."})
    # A failed NS lookup is harmless when the registry already supplied the nameservers.
    relevant = {"A", "AAAA", "MX"} | ({"NS"} if ns_source != "registry" else set())
    failed_types = sorted({t for rec in (domain_records, host_records) for t in rec.get("errors", {}) if t in relevant})
    if failed_types and not domain_records.get("nxdomain"):
        errors.append({"section": "dns", "code": "lookup", "message": f"DNS lookup failed for: {', '.join(failed_types)}."})
    tls_data = report["tls_certificate"]
    if secure and tls_data.get("error") and not tls_data.get("connection_verified") and response.tls_verified is not False:
        errors.append({"section": "tls", "code": "failed", "message": tls_data["error"]})
    if reg.get("error"):
        errors.append({"section": "registration", "code": "lookup", "message": reg["error"]})
    archive_error = report["website_history"].get("error")
    if archive_error and not report["website_history"].get("earliest_capture_utc"):
        errors.append({"section": "archive", "code": "unavailable", "message": archive_error})

    report["technologies"] = _section(
        "technologies",
        lambda: tech.detect_technologies(body_text if is_html else "", headers, cookies, page, host_records),
        timings, [], errors,
    )
    if wordpress.get("detected"):
        for item in report["technologies"]:
            if (item["category"], item["name"]) == ("CMS", "WordPress") and wordpress.get("version"):
                item["version"] = wordpress["version"]

    # A name missing from public DNS but served over HTTP resolves locally (hosts file, dev proxy): not a failure.
    unreachable = response.error is not None or not response.status
    if unreachable:
        # TLS and archive failures are consequences of the site being down, not separate problems.
        errors[:] = [e for e in errors if e["section"] not in {"tls", "archive"}]
        return finish("failed", exitcodes.FAILED)
    if errors:
        return finish("partial", exitcodes.PARTIAL)
    return finish("ok", exitcodes.OK)
