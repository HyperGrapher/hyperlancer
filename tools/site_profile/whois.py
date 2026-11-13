"""WHOIS text parsing, including the TRABIS (.tr) layout."""

from __future__ import annotations

import datetime as dt
import re
import socket
from typing import Any


def parse_date(value: str) -> str | None:
    value = value.strip().strip(". ")
    candidates = [value]
    candidates.extend(re.findall(r"\d{4}[-/.][A-Za-z0-9]{1,3}[-/.]\d{1,4}(?:T[^\s]+)?", value))
    candidates.extend(re.findall(r"\d{1,2}-[A-Za-z]{3}-\d{4}", value))
    formats = (
        "%Y-%m-%dT%H:%M:%SZ", "%Y-%m-%dT%H:%M:%S%z", "%Y-%m-%d", "%Y/%m/%d", "%Y.%m.%d",
        "%Y-%b-%d", "%Y-%B-%d", "%d-%b-%Y", "%d-%B-%Y",
    )
    for candidate in candidates:
        candidate = candidate.strip().rstrip(".")
        try:
            return dt.datetime.fromisoformat(candidate.replace("Z", "+00:00")).date().isoformat()
        except ValueError:
            pass
        for date_format in formats:
            try:
                return dt.datetime.strptime(candidate, date_format).date().isoformat()
            except ValueError:
                continue
    return None


def whois_query(domain: str, server: str, timeout: float) -> str:
    with socket.create_connection((server, 43), timeout=timeout) as connection:
        connection.settimeout(timeout)
        connection.sendall(domain.encode("idna") + b"\r\n")
        chunks: list[bytes] = []
        received = 0
        while received < 1_000_000:
            chunk = connection.recv(min(65536, 1_000_000 - received))
            if not chunk:
                break
            chunks.append(chunk)
            received += len(chunk)
    return b"".join(chunks).decode("utf-8", errors="replace")


DATE_LABELS = {
    "creationdate": "registration_date", "created": "registration_date", "createdon": "registration_date",
    "registered": "registration_date", "registrationdate": "registration_date", "domaincreated": "registration_date",
    "domaincreatedate": "registration_date",
    "expirydate": "expiration_date", "expirationdate": "expiration_date", "registryexpirydate": "expiration_date",
    "expires": "expiration_date", "expireson": "expiration_date",
    "updateddate": "last_updated", "lastupdated": "last_updated", "lastupdatetime": "last_updated",
    "lastmodified": "last_updated",
}

_TRABIS_HEADER = re.compile(r"^\*\*\s*([^:]+?)\s*(?::\s*(.*))?$")


HIDDEN = re.compile(r"hidden|redacted|privacy|not disclosed|withheld", re.I)


def parse_trabis_blocks(record: str) -> dict[str, Any]:
    """TRABIS groups data under `** Registrant:`, `** Registrar:` and `** Domain Servers:` headings."""
    out: dict[str, Any] = {"registrar": None, "nameservers": [], "registrant": None}
    block: str | None = None
    for raw in record.splitlines():
        line = raw.rstrip()
        header = _TRABIS_HEADER.match(line.strip())
        if header and line.strip().startswith("**"):
            block = header.group(1).strip().lower()
            inline = (header.group(2) or "").strip()
            if block == "registrar" and inline and not out["registrar"]:
                out["registrar"] = inline
            continue
        stripped = line.strip()
        if not stripped:
            if block == "domain servers" and out["nameservers"]:
                block = None
            continue
        if block == "registrant" and not out["registrant"] and not HIDDEN.search(stripped):
            out["registrant"] = stripped  # first line of the block is the owner's name; the rest is address data
        elif block == "registrar" and not out["registrar"]:
            label, _, value = stripped.partition(":")
            if re.sub(r"[^a-z]", "", label.lower()) in {"organizationname", "organisationname", "name"} and value.strip():
                out["registrar"] = value.strip()
        elif block == "domain servers":
            token = stripped.split()[0].lower().rstrip(".")
            if re.fullmatch(r"[a-z0-9.-]+\.[a-z]{2,}", token):
                out["nameservers"].append(token)
    # Fall back to the first plain line of the Registrar block when no "Organization Name" label exists.
    if not out["registrar"]:
        match = re.search(r"\*\*\s*Registrar:?\s*\n((?:[ \t]*[^\n*]+\n?)+)", record)
        if match:
            for line in match.group(1).splitlines():
                if line.strip() and ":" not in line:
                    out["registrar"] = line.strip()
                    break
    return out


def transfer_locked(statuses: list[str], transfer_text: str | None = None) -> bool | None:
    """True when the registry blocks transfers (EPP *TransferProhibited, or TRABIS "LOCKED"); None if unknown."""
    if transfer_text:
        lowered = transfer_text.lower()
        if "unlocked" in lowered or "not locked" in lowered or "is open" in lowered:
            return False
        if "locked" in lowered:
            return True
    lowered_statuses = " ".join(statuses).lower().replace(" ", "")
    if "transferprohibited" in lowered_statuses:
        return True
    # Only EPP-style statuses are evidence of "not locked"; a bare "Active" says nothing about transfers.
    epp = any(re.search(r"prohibited|^ok\b|pending|hold|redemption", item, re.I) for item in statuses)
    return False if epp else None


def parse_whois_record(record: str) -> dict[str, Any]:
    result: dict[str, Any] = {
        "registration_date": None, "expiration_date": None, "last_updated": None,
        "registrar": None, "nameservers": [], "statuses": [], "registrant": None, "transfer_status": None,
    }
    for line in record.splitlines():
        if ":" not in line:
            continue
        raw_label, value = line.split(":", 1)
        label = re.sub(r"[^a-z]", "", raw_label.lower())
        value = value.strip()
        target = DATE_LABELS.get(label)
        if target and not result[target]:
            result[target] = parse_date(value)
        elif label in {"registrar", "registrarname"} and value and not result["registrar"]:
            result["registrar"] = value
        elif label in {"nameserver", "nserver"} and value:
            result["nameservers"].append(value.split()[0].lower().rstrip("."))
        elif label in {"domainstatus", "status"} and value:
            result["statuses"].append(value)
        elif label in {"registrantorganization", "registrantorganisation", "registrantname", "registrantorg"} and value \
                and not result["registrant"] and not HIDDEN.search(value):
            result["registrant"] = value
        elif label == "transferstatus" and value and not result["transfer_status"]:
            result["transfer_status"] = value

    trabis = parse_trabis_blocks(record)
    if not result["registrar"] and trabis["registrar"]:
        result["registrar"] = trabis["registrar"]
    if not result["registrant"] and trabis["registrant"]:
        result["registrant"] = trabis["registrant"]
    result["nameservers"].extend(trabis["nameservers"])
    result["nameservers"] = sorted(set(result["nameservers"]))
    result["statuses"] = sorted(set(result["statuses"]))
    return result
