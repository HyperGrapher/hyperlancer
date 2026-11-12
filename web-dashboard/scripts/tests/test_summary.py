"""The normalized summary the desktop app stores (site_profile/summary.py)."""

from __future__ import annotations

import json
from pathlib import Path

from site_profile.summary import SUMMARY_VERSION, summarize

EXAMPLES = Path(__file__).resolve().parent.parent / "examples"


def _minimal_failed() -> dict:
    return {"schema_version": 2, "status": "failed", "errors": [{"section": "http", "code": "dns", "message": "NXDOMAIN"}]}


def test_failed_report_has_every_section():
    summary = summarize(_minimal_failed())
    assert summary["summaryVersion"] == SUMMARY_VERSION
    assert summary["outcome"] == "failed"
    assert summary["errors"][0]["message"] == "NXDOMAIN"
    for key in ("url", "http", "tech", "cms", "hosting", "dns", "tls", "domain", "seo", "sitemaps",
                "contact", "security", "archive", "design", "facts"):
        assert key in summary
    assert summary["facts"]["http_status"] is None
    assert summary["cms"]["name"] is None


def test_unknown_status_becomes_failed():
    assert summarize({"status": "weird"})["outcome"] == "failed"


def test_mx_parsed_and_sorted():
    raw = {"status": "ok", "dns": {"domain_records": {"records": {"MX": ["20 b.mx.example.", "10 A.mx.example."]}}}}
    assert summarize(raw)["dns"]["mx"] == [{"pref": 10, "host": "a.mx.example"}, {"pref": 20, "host": "b.mx.example"}]


def test_facts_strip_country_and_as_prefix():
    raw = {"status": "ok", "hosting": {"as_name": "NATRO - Natro Hosting, TR", "ips": ["1.2.3.4"]},
           "tls_certificate": {"valid_until_utc": "2027-01-02T00:00:00Z", "issuer": "countryName=US, organizationName=Let's Encrypt, commonName=R3"}}
    summary = summarize(raw)
    assert summary["facts"]["hosting"] == "Natro Hosting"
    assert summary["facts"]["ip_address"] == "1.2.3.4"
    assert summary["facts"]["tls_valid_until"] == "2027-01-02"
    assert summary["tls"]["issuer"] == "Let's Encrypt"


def test_examples_summarize_without_errors():
    for path in EXAMPLES.glob("*.json"):
        raw = json.loads(path.read_text(encoding="utf-8"))
        summary = summarize(raw)
        json.dumps(summary)  # serializable
        assert summary["outcome"] in ("ok", "partial", "failed")
