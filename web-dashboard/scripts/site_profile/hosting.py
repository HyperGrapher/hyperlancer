"""Where the site is hosted (IP, reverse DNS, ASN via Team Cymru's DNS service) and who handles its email."""

from __future__ import annotations

import ipaddress
import socket
from typing import Any

import dns.exception
import dns.resolver
import dns.reversename

from .dnsinfo import make_resolver

# Substring of the MX host (lower-case) -> provider.
EMAIL_PROVIDERS = (
    ("aspmx.l.google.com", "Google Workspace"), ("googlemail.com", "Google Workspace"), ("google.com", "Google Workspace"),
    ("mail.protection.outlook.com", "Microsoft 365"), ("outlook.com", "Microsoft 365"),
    ("zoho.", "Zoho Mail"), ("yandex.", "Yandex Mail"), ("protonmail.ch", "Proton Mail"), ("proton.me", "Proton Mail"),
    ("mimecast", "Mimecast"), ("secureserver.net", "GoDaddy"), ("yahoodns.net", "Yahoo"),
    ("icloud.com", "iCloud"), ("messagingengine.com", "Fastmail"), ("mailgun.org", "Mailgun"),
    ("natrohost.com", "Natro"), ("hostinger", "Hostinger"), ("ovh.net", "OVH"), ("one.com", "one.com"),
    ("emailsrvr.com", "Rackspace"), ("registrar-servers.com", "Namecheap"), ("improvmx.com", "ImprovMX"),
)


def email_provider(mx_hosts: list[str]) -> str | None:
    for host in mx_hosts:
        lowered = host.lower()
        for marker, provider in EMAIL_PROVIDERS:
            if marker in lowered:
                return provider
    return None


def parse_mx(values: list[str]) -> list[dict[str, Any]]:
    parsed: list[dict[str, Any]] = []
    for value in values:
        pref, _, host = value.partition(" ")
        if pref.isdigit() and host:
            parsed.append({"pref": int(pref), "host": host.lower()})
    return sorted(parsed, key=lambda item: (item["pref"], item["host"]))


def profile_email(domain_records: dict[str, Any]) -> dict[str, Any]:
    mx = parse_mx(domain_records.get("records", {}).get("MX", []))
    return {"provider": email_provider([item["host"] for item in mx]), "mx": mx}


def _txt(name: str, timeout: float, resolver_ip: str | None) -> list[str]:
    try:
        answer = make_resolver(timeout, resolver_ip).resolve(name, "TXT", raise_on_no_answer=False)
        return [b"".join(item.strings).decode("utf-8", errors="replace") for item in answer] if answer.rrset else []
    except (dns.exception.DNSException, OSError):
        return []


def _cymru(ip: str, timeout: float, resolver_ip: str | None) -> dict[str, Any]:
    """ASN, prefix, country and AS name through Team Cymru's DNS interface (IPv4 and IPv6)."""
    address = ipaddress.ip_address(ip)
    if address.version == 4:
        query = ".".join(reversed(ip.split("."))) + ".origin.asn.cymru.com"
    else:
        query = ".".join(reversed(address.exploded.replace(":", ""))) + ".origin6.asn.cymru.com"
    rows = _txt(query, timeout, resolver_ip)
    if not rows:
        return {}
    fields = [part.strip() for part in rows[0].split("|")]
    asn = fields[0].split()[0] if fields and fields[0] else None
    info: dict[str, Any] = {"asn": f"AS{asn}" if asn else None, "prefix": fields[1] if len(fields) > 1 else None,
                            "country": fields[2] if len(fields) > 2 else None, "as_name": None}
    if asn:
        names = _txt(f"AS{asn}.asn.cymru.com", timeout, resolver_ip)
        if names:
            parts = [part.strip() for part in names[0].split("|")]
            info["as_name"] = parts[4] if len(parts) > 4 else None
    return info


def reverse_dns(ip: str, timeout: float, resolver_ip: str | None) -> str | None:
    try:
        answer = make_resolver(timeout, resolver_ip).resolve(dns.reversename.from_address(ip), "PTR", raise_on_no_answer=False)
        return answer[0].to_text().rstrip(".") if answer.rrset else None
    except (dns.exception.DNSException, OSError):
        return None


def profile_hosting(host_records: dict[str, Any], host: str, timeout: float, resolver_ip: str | None = None) -> dict[str, Any]:
    records = host_records.get("records", {})
    ipv4 = list(records.get("A", []))
    ipv6 = list(records.get("AAAA", []))
    if not ipv4 and not ipv6:  # DNS section failed or timed out: ask the OS resolver before giving up
        try:
            for info in socket.getaddrinfo(host, None, type=socket.SOCK_STREAM):
                address = str(info[4][0])
                (ipv6 if ":" in address else ipv4).append(address)
            ipv4, ipv6 = sorted(set(ipv4)), sorted(set(ipv6))
        except socket.gaierror:
            pass
    result: dict[str, Any] = {
        "ips": ipv4[:10], "ipv6": ipv6[:10], "reverse_dns": None,
        "asn": None, "as_name": None, "prefix": None, "country": None, "source": None,
    }
    primary = (ipv4 or ipv6 or [None])[0]
    if primary:
        result["reverse_dns"] = reverse_dns(primary, timeout, resolver_ip)
        info = _cymru(primary, timeout, resolver_ip)
        if info:
            result.update(info)
            result["source"] = "Team Cymru IP-to-ASN (DNS)"
    return result
