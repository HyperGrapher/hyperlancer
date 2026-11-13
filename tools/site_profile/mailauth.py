"""Email authentication: SPF, DMARC and DKIM, with a verdict per record and an overall label.

DKIM cannot be listed in DNS, only probed: a list of common selectors is queried, starting with
the ones that match the detected mail provider. "No common selector answered" is reported as
unknown, never as "DKIM missing".
"""

from __future__ import annotations

import re
from concurrent.futures import ThreadPoolExecutor
from typing import Any, Callable

from . import dnsinfo

SPF_LOOKUP_LIMIT = 10
SPF_QUERY_BUDGET = 12  # DNS queries spent resolving include:/redirect= chains
COMMON_SELECTORS = ("default", "k1", "s1", "s2", "mail", "dkim", "smtp", "zoho", "mandrill", "key1", "protonmail")
PROVIDER_SELECTORS = {
    "google": ("google",),
    "microsoft": ("selector1", "selector2"),
    "zoho": ("zoho", "zmail"),
    "yandex": ("mail",),
}

Lookup = Callable[[str], list[str]]


def parse_spf(record: str) -> dict[str, Any]:
    terms = record.split()[1:]
    qualifier = None
    includes: list[str] = []
    redirect = None
    direct = 0  # a, mx, ptr, exists: each costs one lookup but is not resolved further
    for term in terms:
        lowered = term.lower()
        if lowered.endswith("all") and re.fullmatch(r"[+\-~?]?all", lowered):
            qualifier = lowered[0] if lowered[0] in "+-~?" else "+"
        elif lowered.startswith("include:"):
            includes.append(term.split(":", 1)[1])
        elif lowered.startswith("redirect="):
            redirect = term.split("=", 1)[1]
        elif re.match(r"[+\-~?]?(a|mx|ptr|exists)([:/]|$)", lowered):
            direct += 1
    return {"qualifier": qualifier, "includes": includes, "redirect": redirect, "direct_lookups": direct}


def count_spf_lookups(record: str, lookup_txt: Lookup, budget: int = SPF_QUERY_BUDGET) -> tuple[int, bool]:
    """Lookups an SPF check would spend, following include/redirect. Returns (count, complete)."""
    spent = {"queries": 0, "complete": True}
    seen: set[str] = set()

    def walk(text: str) -> int:
        parsed = parse_spf(text)
        total = parsed["direct_lookups"]
        for target in [*parsed["includes"], *([parsed["redirect"]] if parsed["redirect"] else [])]:
            total += 1
            key = target.lower()
            if key in seen:
                continue
            seen.add(key)
            if spent["queries"] >= budget:
                spent["complete"] = False
                continue
            spent["queries"] += 1
            for value in lookup_txt(target):
                if value.lower().startswith("v=spf1"):
                    total += walk(value)
                    break
        return total

    return walk(record), spent["complete"]


def spf_verdict(records: list[str], lookups: int | None) -> dict[str, Any]:
    if not records:
        return {"verdict": "missing", "issues": ["No SPF record."]}
    issues: list[str] = []
    if len(records) > 1:
        issues.append("More than one SPF record: receivers treat this as an error.")
    parsed = parse_spf(records[0])
    qualifier = parsed["qualifier"]
    if qualifier is None and not parsed["redirect"]:
        issues.append("No \"all\" mechanism, so any sender is allowed.")
    elif qualifier == "+":
        issues.append("\"+all\" allows every sender.")
    elif qualifier == "?":
        issues.append("\"?all\" is neutral: it does not tell receivers to reject anything.")
    if lookups is not None and lookups > SPF_LOOKUP_LIMIT:
        issues.append(f"{lookups} DNS lookups, over the limit of {SPF_LOOKUP_LIMIT}.")
    return {"verdict": "weak" if issues else "pass", "issues": issues, "qualifier": qualifier,
            "includes": parsed["includes"]}


def parse_dmarc(record: str) -> dict[str, str]:
    tags: dict[str, str] = {}
    for part in record.split(";"):
        if "=" in part:
            key, value = part.split("=", 1)
            tags[key.strip().lower()] = value.strip()
    return tags


def dmarc_verdict(records: list[str]) -> dict[str, Any]:
    if not records:
        return {"verdict": "missing", "issues": ["No DMARC record."]}
    tags = parse_dmarc(records[0])
    policy = tags.get("p", "").lower()
    issues: list[str] = []
    if policy == "none" or not policy:
        issues.append("Policy is \"none\": mail that fails the checks is still delivered.")
    pct = tags.get("pct")
    if pct and pct.isdigit() and int(pct) < 100 and policy in ("quarantine", "reject"):
        issues.append(f"The policy applies to only {pct}% of mail.")
    return {"verdict": "weak" if issues else "pass", "issues": issues, "policy": policy or None,
            "subdomainPolicy": tags.get("sp"), "pct": tags.get("pct"), "rua": tags.get("rua"),
            "ruf": tags.get("ruf"), "adkim": tags.get("adkim"), "aspf": tags.get("aspf")}


def selectors_for(provider: str | None, extra: tuple[str, ...] = ()) -> list[str]:
    ordered: list[str] = []
    name = (provider or "").lower()
    for hint, selectors in PROVIDER_SELECTORS.items():
        if hint in name:
            ordered.extend(selectors)
    ordered.extend(extra)
    ordered.extend(COMMON_SELECTORS)
    return list(dict.fromkeys(ordered))


def parse_dkim_key(record: str) -> dict[str, Any] | None:
    """Key type and size from a DKIM TXT record; None when it is not a DKIM key record."""
    tags = {k.strip().lower(): v.strip() for k, _, v in (part.partition("=") for part in record.split(";")) if k.strip()}
    key = re.sub(r"\s+", "", tags.get("p", ""))
    if not key and "v" not in tags and "k" not in tags:
        return None
    key_type = (tags.get("k") or "rsa").lower()
    bits = None
    if key and key_type == "rsa":
        # A DER-encoded 2048-bit RSA public key is about 392 base64 characters, 1024-bit about 216.
        bits = 4096 if len(key) > 700 else 2048 if len(key) > 300 else 1024 if len(key) > 150 else 512
    return {"type": key_type, "bits": bits, "revoked": not key}


def probe_dkim(domain: str, provider: str | None, lookup_txt: Lookup, extra: tuple[str, ...] = ()) -> dict[str, Any]:
    selectors = selectors_for(provider, extra)

    def probe(selector: str) -> dict[str, Any] | None:
        for value in lookup_txt(f"{selector}._domainkey.{domain}"):
            info = parse_dkim_key(value)
            if info and not info["revoked"]:
                return {"selector": selector, **info}
        return None

    with ThreadPoolExecutor(max_workers=3) as pool:
        found = [item for item in pool.map(probe, selectors) if item]
    weak = [item for item in found if item["bits"] and item["bits"] < 1024]
    return {"verdict": "pass" if found and not weak else "weak" if weak else "unknown",
            "found": found, "probed": selectors,
            "issues": ["A DKIM key shorter than 1024 bits."] if weak else []}


def overall(spf: dict[str, Any], dmarc: dict[str, Any]) -> str:
    if spf["verdict"] == "missing" and dmarc["verdict"] == "missing":
        return "missing"
    return "pass" if spf["verdict"] == "pass" and dmarc["verdict"] == "pass" else "weak"


def analyze(domain: str, domain_records: dict[str, Any], provider: str | None, timeout: float,
            resolver_ip: str | None, has_mail: bool = True) -> dict[str, Any]:
    def lookup_txt(name: str) -> list[str]:
        values, _, _ = dnsinfo.lookup(name, "TXT", timeout, resolver_ip)
        return values

    spf_records = list(domain_records.get("spf_records", []))
    lookups, complete = (count_spf_lookups(spf_records[0], lookup_txt) if spf_records else (None, True))
    spf = spf_verdict(spf_records, lookups)
    spf.update(raw=spf_records[0] if spf_records else None, lookups=lookups, lookupsComplete=complete)
    dmarc_records = list(domain_records.get("dmarc_records", []))
    dmarc = dmarc_verdict(dmarc_records)
    dmarc["raw"] = dmarc_records[0] if dmarc_records else None
    dkim = probe_dkim(domain, provider, lookup_txt) if has_mail else {"verdict": "unknown", "found": [], "probed": [], "issues": []}
    return {"spf": spf, "dmarc": dmarc, "dkim": dkim, "overall": overall(spf, dmarc)}


def empty() -> dict[str, Any]:
    return {"spf": {"verdict": "missing", "issues": [], "raw": None}, "dmarc": {"verdict": "missing", "issues": [], "raw": None},
            "dkim": {"verdict": "unknown", "found": [], "probed": [], "issues": []}, "overall": "missing"}
