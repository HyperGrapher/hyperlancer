"""DNS records, with retries and gentle parallelism (a home router chokes on nine parallel queries)."""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
from typing import Any

import dns.exception
import dns.flags
import dns.resolver
import dns.reversename

RECORD_TYPES = ("A", "AAAA", "CNAME", "NS", "MX", "TXT", "CAA", "SOA")
ATTEMPTS = 2
# Used when the computer's own resolver fails (VPNs, odd Windows network setups, routers that drop queries).
PUBLIC_RESOLVERS = ["1.1.1.1", "8.8.8.8", "9.9.9.9"]


def make_resolver(timeout: float, resolver_ip: str | None, dnssec: bool = False, public: bool = False) -> dns.resolver.Resolver:
    try:
        resolver = dns.resolver.Resolver(configure=not public)
    except Exception:  # noqa: BLE001 - an unreadable system DNS setup must not stop the profile
        resolver = dns.resolver.Resolver(configure=False)
        public = True
    resolver.timeout = min(timeout, 3)
    resolver.lifetime = min(timeout, 4)
    if resolver_ip:
        resolver.nameservers = [resolver_ip]
    elif public or not resolver.nameservers:
        resolver.nameservers = list(PUBLIC_RESOLVERS)
    if dnssec:
        resolver.use_edns(edns=0, ednsflags=dns.flags.DO)
    return resolver


def _values(record_type: str, answer: Any) -> list[str]:
    values: list[str] = []
    for item in answer:
        if record_type == "MX":
            values.append(f"{item.preference} {item.exchange.to_text().rstrip('.')}")
        elif record_type == "TXT":
            values.append(b"".join(item.strings).decode("utf-8", errors="replace"))
        else:
            values.append(item.to_text().rstrip("."))
    return values


def resolve(name: str, record_type: str, timeout: float, resolver_ip: str | None, dnssec: bool = False) -> Any:
    """dnspython's answer. The system resolver goes first. When it fails, or says the name does not exist
    (a local filter, a VPN or a broken router can say so for real names), the public resolvers get a turn,
    unless the user chose a resolver."""
    try:
        return make_resolver(timeout, resolver_ip, dnssec).resolve(name, record_type, raise_on_no_answer=False)
    except dns.resolver.NoAnswer:
        raise
    except (dns.exception.DNSException, OSError):  # includes NXDOMAIN
        if resolver_ip:
            raise
    return make_resolver(timeout, None, dnssec, public=True).resolve(name, record_type, raise_on_no_answer=False)


def lookup(name: str, record_type: str, timeout: float, resolver_ip: str | None) -> tuple[list[str], str | None, bool]:
    """Returns (values, error, nxdomain). Timeouts are retried; NXDOMAIN/NoAnswer are not errors."""
    last_error: str | None = None
    for _ in range(ATTEMPTS):
        try:
            answer = resolve(name, record_type, timeout, resolver_ip, dnssec=record_type == "DNSKEY")
            return (_values(record_type, answer) if answer.rrset is not None else []), None, False
        except dns.resolver.NXDOMAIN:
            return [], None, True
        except dns.resolver.NoAnswer:
            return [], None, False
        except (dns.exception.DNSException, OSError) as exc:
            last_error = f"{type(exc).__name__}: {exc}" if str(exc) else type(exc).__name__
    return [], last_error, False


def empty_records(name: str | None) -> dict[str, Any]:
    return {
        "queried_name": name,
        "records": {t: [] for t in (*RECORD_TYPES, "DNSKEY")},
        "spf_records": [],
        "dmarc_records": [],
        "dnskey_present": False,
        "nxdomain": False,
        "errors": {},
    }


def profile_dns(name: str, timeout: float, resolver_ip: str | None = None) -> dict[str, Any]:
    result = empty_records(name)
    records: dict[str, list[str]] = result["records"]
    # Three workers instead of nine: friendlier to consumer routers. DNSKEY is optional and never an error.
    with ThreadPoolExecutor(max_workers=3) as pool:
        futures = {t: pool.submit(lookup, name, t, timeout, resolver_ip) for t in (*RECORD_TYPES, "DNSKEY")}
        dmarc_future = pool.submit(lookup, f"_dmarc.{name}", "TXT", timeout, resolver_ip)
        for record_type, future in futures.items():
            values, error, nxdomain = future.result()
            records[record_type] = values[:30]
            if nxdomain:
                result["nxdomain"] = True
            if error and record_type != "DNSKEY":
                result["errors"][record_type] = error
        dmarc_values, _, _ = dmarc_future.result()
    result["dmarc_records"] = [v for v in dmarc_values if v.lower().startswith("v=dmarc1")]
    result["spf_records"] = [v for v in records["TXT"] if v.lower().startswith("v=spf1")]
    result["dnskey_present"] = bool(records["DNSKEY"])
    return result
