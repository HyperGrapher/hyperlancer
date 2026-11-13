import io
import json
from pathlib import Path

import jsonschema
import pytest

from site_profile import archive, cli, core, design, dnsinfo, hosting, registration, sitemaps, tls
from site_profile import http as http_mod
from site_profile.http import Response
from site_tools import exitcodes
from site_tools.cancel import Cancelled, CancelToken
from site_tools.events import Emitter

SCHEMA = json.loads((Path(core.__file__).parent / "schema_v2.json").read_text())
HTML = (Path(__file__).parent / "fixtures" / "html" / "agency.html").read_text(encoding="utf-8")


def validate(report):
    jsonschema.validate(report, SCHEMA)


@pytest.fixture
def offline(monkeypatch):
    """Replace every network-facing section with canned data; individual tests override pieces."""
    monkeypatch.setattr(core, "check_url", lambda url, allow_private=False: None)
    monkeypatch.setattr(core, "fetch", lambda url, **o: Response(
        url="https://www.forazeytin.com.tr/", status=200, body=HTML.encode(), ttfb_ms=120, total_ms=300, tls_verified=True,
        headers={"content-type": "text/html; charset=utf-8", "server": "LiteSpeed", "content-encoding": "gzip", "x-powered-by": "PHP/7.4.33"},
        set_cookies=["PHPSESSID=abc; path=/; Secure; HttpOnly; SameSite=Lax"],
        redirects=[{"status": 301, "from": url, "to": "https://www.forazeytin.com.tr/"}],
    ))
    monkeypatch.setattr(core, "http_checks", lambda *a, **k: {"https_redirect": True, "www_canonical": "www", "www_redirect": True})

    def fake_dns(name, timeout, resolver=None):
        rec = dnsinfo.empty_records(name)
        rec["records"].update(A=["89.19.30.15"], MX=["10 mx.natrohost.com"], TXT=["v=spf1 -all"])
        rec["spf_records"] = ["v=spf1 -all"]
        return rec

    monkeypatch.setattr(core.dnsinfo, "profile_dns", fake_dns)
    monkeypatch.setattr(core.hosting, "profile_hosting", lambda *a, **k: {
        "ips": ["89.19.30.15"], "ipv6": [], "reverse_dns": None, "asn": "AS34619", "as_name": "NATRO", "prefix": None, "country": "TR", "source": "test"})
    monkeypatch.setattr(core.tls, "profile_tls", lambda *a, **k: {**tls.empty(), "connection_verified": True, "issuer": "Sectigo", "days_until_expiry": 61.0})
    reg = registration.empty("forazeytin.com.tr")
    reg.update(lookup_method="WHOIS", registrar="Natro", expiration_date="2027-07-05", registration_date="2000-07-06", nameservers=["ns1.natrohost.com"])
    monkeypatch.setattr(core.registration, "profile_registration", lambda *a, **k: dict(reg))
    monkeypatch.setattr(core.sitemaps, "profile_sitemaps", lambda *a, **k: ({
        "robots": {**sitemaps.empty()["robots"], "status": 200},
        "sitemaps": {"files": [], "total_urls": 0, "urls": [], "truncated": False}}, []))
    cap = archive.empty()
    cap.update(earliest_capture_utc="2009-03-01T00:00:00+00:00", earliest_capture_year=2009)
    monkeypatch.setattr(core.archive, "profile_archive", lambda hosts: dict(cap))
    monkeypatch.setattr(core.design, "collect", lambda *a, **k: design.summarize([
        design._clue("wp-uploads", "Oldest WordPress upload on the home page", "2018-08", None, "strong")]))
    return monkeypatch


def run(url="forazeytin.com.tr", cancel=None):
    return core.collect(core.ProfileParams(url=url), Emitter(enabled=False), cancel or CancelToken())


def test_ok_report_matches_schema(offline):
    report, code = run()
    assert (report["status"], code) == ("ok", exitcodes.OK)
    validate(report)
    assert report["errors"] == []
    assert report["http"]["compression"] == "gzip" and report["http"]["ttfb_ms"] == 120
    assert report["http"]["cookies"][0] == {"name": "PHPSESSID", "secure": True, "http_only": True, "same_site": "Lax"}
    assert report["http"]["notable_headers"]["set-cookie"].startswith("[redacted")
    assert report["email"] == {"provider": "Natro", "mx": [{"pref": 10, "host": "mx.natrohost.com"}]}
    assert report["dns"]["ns"] == {"values": ["ns1.natrohost.com"], "source": "registry"}  # DNS gave no NS: registry fallback
    assert report["wordpress"]["theme"] == "astra"
    wp = next(t for t in report["technologies"] if t["name"] == "WordPress")
    assert wp["version"] == "6.6.1"
    assert report["contacts"]["emails"]
    assert set(report["timings_ms"]) >= {"http", "page", "dns", "registration"}
    assert report["design"]["estimated_from"] == "2018-08"
    kinds = {c["kind"] for c in report["design"]["clues"]}
    assert {"archive", "domain"} <= kinds  # context from the Wayback and registration sections


def test_partial_when_a_secondary_section_fails(offline):
    offline.setattr(core.archive, "profile_archive", lambda hosts: archive.empty("ConnectTimeout"))
    report, code = run()
    assert (report["status"], code) == ("partial", exitcodes.PARTIAL)
    assert [(e["section"], e["code"]) for e in report["errors"]] == [("archive", "unavailable")]
    validate(report)


def test_failed_ns_lookup_is_not_an_error_when_registry_supplies_ns(offline):
    def dns_without_ns(name, timeout, resolver=None):
        rec = dnsinfo.empty_records(name)
        rec["records"].update(A=["89.19.30.15"])
        rec["errors"] = {"NS": "LifetimeTimeout"}
        return rec

    offline.setattr(core.dnsinfo, "profile_dns", dns_without_ns)
    report, code = run()
    assert (report["dns"]["ns"]["source"], code) == ("registry", exitcodes.OK)
    offline.setattr(core.registration, "profile_registration", lambda *a, **k: registration.empty("forazeytin.com.tr"))
    report, code = run()
    assert code == exitcodes.PARTIAL and any(e["code"] == "lookup" for e in report["errors"])


def test_section_exception_becomes_error_not_crash(offline):
    def boom(*a, **k):
        raise RuntimeError("kaput")

    offline.setattr(core.registration, "profile_registration", boom)
    report, code = run()
    assert code == exitcodes.PARTIAL
    assert any(e["section"] == "registration" and e["code"] == "exception" for e in report["errors"])
    validate(report)


def test_unreachable_site_is_failed_but_still_shaped(offline):
    offline.setattr(core, "fetch", lambda url, **o: Response(url=url, error="ConnectError: refused", error_code="connect"))
    offline.setattr(core.tls, "profile_tls", lambda *a, **k: tls.empty("refused"))
    offline.setattr(core.archive, "profile_archive", lambda hosts: archive.empty("timeout"))
    report, code = run()
    assert (report["status"], code) == ("failed", exitcodes.FAILED)
    assert {e["section"] for e in report["errors"]} == {"http"}  # tls/archive noise is dropped
    assert report["page"]["parseable_html"] is False and report["technologies"] == []
    validate(report)


def test_name_that_only_resolves_locally_is_not_a_failure(offline):
    def nx(name, timeout, resolver=None):
        rec = dnsinfo.empty_records(name)
        rec["nxdomain"] = True
        return rec

    offline.setattr(core.dnsinfo, "profile_dns", nx)
    report, code = run("fora-react.test")
    assert report["status"] != "failed" and code != exitcodes.FAILED
    assert not any(e["code"] == "nxdomain" for e in report["errors"])
    # ...but the same NXDOMAIN with an unreachable site stays a failure.
    offline.setattr(core, "fetch", lambda url, **o: Response(url=url, error="ConnectError", error_code="connect"))
    report, code = run("fora-react.test")
    assert code == exitcodes.FAILED and any(e["code"] == "nxdomain" for e in report["errors"])


def test_bot_protection_is_flagged_as_partial(offline):
    offline.setattr(core, "fetch", lambda url, **o: Response(
        url=url, status=403, body=b"<html><title>Just a moment...</title></html>", headers={"server": "cloudflare", "content-type": "text/html"}))
    report, code = run()
    assert code == exitcodes.PARTIAL
    assert report["http"]["bot_protection"]["provider"] == "Cloudflare"
    assert any(e["code"] == "bot_protection" for e in report["errors"])


def test_non_html_response(offline):
    offline.setattr(core, "fetch", lambda url, **o: Response(url=url, status=200, body=b"%PDF-1.4", headers={"content-type": "application/pdf"}))
    report, code = run()
    assert report["page"]["parseable_html"] is False
    assert any(e["code"] == "not_html" for e in report["errors"])
    validate(report)


def test_private_target_is_blocked_with_exit_4():
    report, code = core.collect(core.ProfileParams(url="http://127.0.0.1:8787/"), Emitter(enabled=False), CancelToken())
    assert code == exitcodes.BLOCKED and report["status"] == "failed"
    assert report["errors"][0]["code"] == "blocked"
    validate(report)


def test_invalid_url_raises_value_error():
    with pytest.raises(ValueError):
        run("ftp://example.com")


def test_cancel_before_start(offline, tmp_path):
    flag = tmp_path / "cancel"
    flag.touch()
    with pytest.raises(Cancelled):
        run(cancel=CancelToken(flag))


def test_progress_events_cover_all_phases(offline):
    out = io.StringIO()
    core.collect(core.ProfileParams(url="forazeytin.com.tr"), Emitter(stream=out), CancelToken())
    labels = [json.loads(l)["label"] for l in out.getvalue().splitlines()]
    assert labels[0] == "resolving" and labels[1] == "resolving"
    assert {"http", "page", "dns", "tls", "registration", "sitemaps", "archive"} <= set(labels)


# ---- TLS fallback in http.fetch ---------------------------------------------------------------------------------

def test_fetch_retries_without_verification_on_certificate_error(monkeypatch):
    calls = []

    def fake(url, **options):
        calls.append(options.get("verify", True))
        if options.get("verify", True):
            return Response(url=url, error="SSLError: CERTIFICATE_VERIFY_FAILED self-signed", error_code="tls")
        return Response(url=url, status=200, body=b"<html></html>", tls_verified=True)

    monkeypatch.setattr(http_mod, "fetch_once", fake)
    response = http_mod.fetch("https://self-signed.example/", timeout=5, max_bytes=1000)
    assert calls == [True, False]
    assert response.ok and response.tls_verified is False and "CERTIFICATE_VERIFY_FAILED" in response.tls_error


def test_classify_error():
    assert http_mod.classify_error(OSError("[Errno -2] Name or service not known"))[0] == "dns"
    assert http_mod.classify_error(OSError("ssl handshake failure"))[0] == "tls"


def test_fetch_blocks_private_redirect_hop(monkeypatch):
    class Boom(Exception):
        pass

    from site_tools import netguard
    from site_profile import http as h

    seen = []

    def check(url, allow_private=False):
        seen.append(url)
        if "169.254" in url:
            raise netguard.BlockedTarget("blocked", "private_network")

    monkeypatch.setattr(h.netguard, "check_url", check)
    response = h.fetch_once("http://169.254.169.254/latest", timeout=2, max_bytes=100)
    assert response.error_code == "blocked" and seen == ["http://169.254.169.254/latest"]


# ---- CLI --------------------------------------------------------------------------------------------------------

def test_cli_spec_mode_writes_report_and_result_event(offline, tmp_path, capsys):
    spec = tmp_path / "spec.json"
    spec.write_text(json.dumps({"jobId": 7, "params": {"url": "forazeytin.com.tr"}, "cancelFile": str(tmp_path / "cancel"), "dir": str(tmp_path)}))
    code = cli.main(["--spec", str(spec)])
    assert code == exitcodes.OK
    validate(json.loads((tmp_path / "profile.json").read_text(encoding="utf-8")))
    events = [json.loads(l) for l in capsys.readouterr().out.splitlines()]
    assert all("type" in e for e in events)  # stdout is NDJSON only
    result = next(e for e in events if e["type"] == "result")
    assert result["path"] == str(tmp_path / "profile.json") and result["status"] == "ok"


def test_cli_exit_codes(offline, tmp_path, capsys):
    assert cli.main([]) == exitcodes.INVALID_INPUT
    assert cli.main(["ftp://x.com"]) == exitcodes.INVALID_INPUT
    assert cli.main(["--spec", str(tmp_path / "missing.json")]) == exitcodes.INVALID_INPUT
    offline.setattr(core.archive, "profile_archive", lambda hosts: archive.empty("timeout"))
    assert cli.main(["forazeytin.com.tr", "--output", str(tmp_path / "r.json")]) == exitcodes.PARTIAL


def test_cli_json_mode_prints_only_json(offline, capsys):
    assert cli.main(["forazeytin.com.tr"]) == exitcodes.OK
    json.loads(capsys.readouterr().out)  # whole stdout is one JSON document


def test_cli_cancel_exit_code(offline, tmp_path):
    flag = tmp_path / "cancel"
    flag.touch()
    spec = tmp_path / "spec.json"
    spec.write_text(json.dumps({"params": {"url": "x.com"}, "cancelFile": str(flag), "dir": str(tmp_path)}))
    assert cli.main(["--spec", str(spec)]) == exitcodes.CANCELLED
    assert not (tmp_path / "profile.json").exists()


# ---- v1 golden files (inputs for the PHP ProfileNormalizer in M3) -------------------------------------------------

V1_KEYS = {"metadata", "input", "domain_registration", "website_history", "http", "page", "dns", "tls_certificate",
           "robots_and_sitemaps", "technologies", "limitations"}


@pytest.mark.parametrize("path", sorted((Path(__file__).parent / "fixtures" / "v1").glob("*.json")))
def test_v1_golden_files_are_intact(path):
    data = json.loads(path.read_text(encoding="utf-8"))
    assert V1_KEYS <= set(data) and "schema_version" not in data
