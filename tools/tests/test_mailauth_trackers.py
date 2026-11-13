"""Offline tests for the email-authentication verdicts and the static tracker detection."""

from site_profile import mailauth, trackers


def txt_lookup(table):
    return lambda name: table.get(name.lower(), [])


def test_spf_hard_fail_is_a_pass():
    result = mailauth.spf_verdict(["v=spf1 include:_spf.google.com -all"], 3)
    assert result["verdict"] == "pass" and result["qualifier"] == "-"


def test_spf_weaknesses():
    assert mailauth.spf_verdict([], None)["verdict"] == "missing"
    assert mailauth.spf_verdict(["v=spf1 a ?all"], 1)["verdict"] == "weak"
    assert mailauth.spf_verdict(["v=spf1 a +all"], 1)["verdict"] == "weak"
    assert mailauth.spf_verdict(["v=spf1 a"], 1)["verdict"] == "weak"
    two = mailauth.spf_verdict(["v=spf1 a -all", "v=spf1 mx -all"], 2)
    assert two["verdict"] == "weak" and any("More than one" in i for i in two["issues"])
    assert mailauth.spf_verdict(["v=spf1 a -all"], 11)["verdict"] == "weak"


def test_spf_lookup_count_follows_includes():
    table = {
        "a.example": ["v=spf1 include:b.example mx -all"],
        "b.example": ["v=spf1 a ip4:1.2.3.4 -all"],
    }
    count, complete = mailauth.count_spf_lookups("v=spf1 include:a.example a -all", txt_lookup(table))
    # include:a (1) + its include:b (1) + mx (1) + b's "a" (1) + the top-level "a" (1)
    assert count == 5 and complete


def test_spf_lookup_budget_marks_result_incomplete():
    table = {f"h{i}.example": [f"v=spf1 include:h{i + 1}.example -all"] for i in range(30)}
    count, complete = mailauth.count_spf_lookups("v=spf1 include:h0.example -all", txt_lookup(table), budget=4)
    assert not complete


def test_dmarc_verdicts():
    assert mailauth.dmarc_verdict([])["verdict"] == "missing"
    weak = mailauth.dmarc_verdict(["v=DMARC1; p=none; rua=mailto:a@x.com"])
    assert weak["verdict"] == "weak" and weak["policy"] == "none" and weak["rua"] == "mailto:a@x.com"
    assert mailauth.dmarc_verdict(["v=DMARC1; p=reject"])["verdict"] == "pass"
    partial = mailauth.dmarc_verdict(["v=DMARC1; p=quarantine; pct=25"])
    assert partial["verdict"] == "weak"


def test_dkim_provider_selectors_come_first():
    assert mailauth.selectors_for("Google Workspace")[0] == "google"
    assert mailauth.selectors_for("Microsoft 365")[:2] == ["selector1", "selector2"]
    assert "default" in mailauth.selectors_for(None)


def test_dkim_probe_finds_keys_and_reports_unknown_when_none():
    key = "v=DKIM1; k=rsa; p=" + "A" * 400
    table = {"google._domainkey.example.com": [key]}
    found = mailauth.probe_dkim("example.com", "Google Workspace", txt_lookup(table))
    assert found["verdict"] == "pass" and found["found"][0]["selector"] == "google"
    nothing = mailauth.probe_dkim("example.com", None, txt_lookup({}))
    assert nothing["verdict"] == "unknown" and nothing["found"] == []


def test_overall_label():
    good_spf, good_dmarc = {"verdict": "pass"}, {"verdict": "pass"}
    assert mailauth.overall(good_spf, good_dmarc) == "pass"
    assert mailauth.overall({"verdict": "missing"}, {"verdict": "missing"}) == "missing"
    assert mailauth.overall(good_spf, {"verdict": "weak"}) == "weak"


HTML = """
<script async src="https://www.googletagmanager.com/gtag/js?id=G-ABC123XYZ9"></script>
<script>gtag('config', 'G-ABC123XYZ9'); gtag('config','AW-123456789');</script>
<script src="https://www.google-analytics.com/analytics.js"></script><script>ga('create','UA-12345-1');</script>
<script>!function(f){fbq('init', '1234567890123456');}</script>
<script src="https://connect.facebook.net/en_US/fbevents.js"></script>
<script>(function(h,o,t,j){h.hj=h.hj;})(window); var s={hjid:998877};</script>
"""


def test_static_trackers_ids_and_flags():
    result = trackers.detect(HTML)
    products = {item["product"]: item for item in result["items"]}
    assert products["Google Analytics 4"]["ids"] == ["G-ABC123XYZ9"]
    assert products["Google Ads"]["ids"] == ["AW-123456789"]
    assert products["Meta Pixel"]["ids"] == ["1234567890123456"]
    assert products["Hotjar"]["ids"] == ["998877"]
    assert products["Universal Analytics"]["status"] == "deprecated"
    assert result["runsAds"] and result["adPlatforms"] == ["Google", "Meta"]
    assert not result["staleAnalytics"]  # GA4 is present next to the old UA tag


def test_stale_analytics_when_only_universal_analytics():
    result = trackers.detect('<script src="https://www.google-analytics.com/analytics.js"></script>')
    assert result["staleAnalytics"] and not result["runsAds"]


def test_nothing_found_in_plain_html():
    result = trackers.detect("<html><body><p>Hello</p></body></html>")
    assert result == trackers.empty()
