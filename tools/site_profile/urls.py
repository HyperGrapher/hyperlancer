from __future__ import annotations

import ipaddress
import re
import urllib.parse
from typing import Any

import tldextract

PSL_PUBLIC = tldextract.TLDExtract(suffix_list_urls=(), include_psl_private_domains=False)
PSL_SITE = tldextract.TLDExtract(suffix_list_urls=(), include_psl_private_domains=True)


def normalize_url(value: str) -> str:
    value = value.strip()
    scheme = re.match(r"^([a-z][a-z0-9+.-]*)://", value, re.IGNORECASE)
    if scheme and scheme.group(1).lower() not in {"http", "https"}:
        raise ValueError("Enter a website URL or hostname using HTTP or HTTPS.")
    if not scheme:
        value = "https://" + value.lstrip("/")
    parts = urllib.parse.urlsplit(value)
    if parts.scheme.lower() not in {"http", "https"} or not parts.hostname:
        raise ValueError("Enter a website URL or hostname using HTTP or HTTPS.")
    if parts.username or parts.password:
        raise ValueError("URLs containing a username or password are not supported.")

    host = parts.hostname.rstrip(".")
    try:
        ipaddress.ip_address(host)
        ascii_host = host
    except ValueError:
        ascii_host = host.encode("idna").decode("ascii").lower()

    port = parts.port
    shown_host = f"[{ascii_host}]" if ":" in ascii_host else ascii_host
    netloc = f"{shown_host}:{port}" if port else shown_host
    path = urllib.parse.quote(parts.path or "/", safe="/%:@!$&'()*+,;=-._~")
    query = urllib.parse.quote(parts.query, safe="=&?/:;,+%@!$'()*-._~")
    return urllib.parse.urlunsplit((parts.scheme.lower(), netloc, path, query, ""))


def get_host(url: str) -> str:
    return urllib.parse.urlsplit(url).hostname or ""


def get_origin(url: str) -> str:
    parts = urllib.parse.urlsplit(url)
    host = parts.hostname or ""
    try:
        host = host.encode("idna").decode("ascii")
    except UnicodeError:
        pass
    if ":" in host:
        host = f"[{host}]"
    if parts.port:
        host = f"{host}:{parts.port}"
    return f"{parts.scheme}://{host}"


def strip_www(host: str) -> str:
    return host[4:] if host.lower().startswith("www.") else host


def domain_parts(host: str) -> dict[str, Any]:
    host = host.rstrip(".").lower()
    try:
        ipaddress.ip_address(host)
        return {
            "host": host,
            "site_domain": host,
            "registrable_domain": None,
            "public_suffix": None,
            "is_ip_address": True,
            "uses_private_suffix": False,
        }
    except ValueError:
        pass

    try:
        ascii_host = host.encode("idna").decode("ascii")
    except UnicodeError:
        ascii_host = host
    site_result = PSL_SITE(ascii_host)
    public_result = PSL_PUBLIC(ascii_host)
    site_domain = site_result.top_domain_under_public_suffix or ascii_host
    registrable_domain = public_result.top_domain_under_public_suffix or site_domain
    return {
        "host": ascii_host,
        "site_domain": site_domain,
        "registrable_domain": registrable_domain,
        "public_suffix": site_result.suffix or None,
        "is_ip_address": False,
        "uses_private_suffix": site_result.is_private,
    }
