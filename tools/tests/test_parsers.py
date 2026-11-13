import gzip
from pathlib import Path

import pytest

from site_profile import page as page_mod
from site_profile import sitemaps, tech, whois
from site_profile.urls import domain_parts, normalize_url

FIX = Path(__file__).parent / "fixtures"


def read(*parts: str) -> str:
    return (FIX.joinpath(*parts)).read_text(encoding="utf-8")


# ---- urls -------------------------------------------------------------------------------------------------------

def test_normalize_url():
    assert normalize_url("Example.COM") == "https://example.com/"
    assert normalize_url("http://çiçek.com.tr/a b?x=1#frag") == "http://xn--iek-1lab.com.tr/a%20b?x=1"
    for bad in ("ftp://x.com", "https://user:pw@x.com", "https://"):
        with pytest.raises(ValueError):
            normalize_url(bad)


def test_domain_parts():
    parts = domain_parts("www.forazeytin.com.tr")
    assert parts["registrable_domain"] == "forazeytin.com.tr"
    assert parts["public_suffix"] == "com.tr"
    assert domain_parts("10.0.0.1")["is_ip_address"] is True


# ---- whois ------------------------------------------------------------------------------------------------------

def test_parse_trabis_record():
    parsed = whois.parse_whois_record(read("whois", "trabis.txt"))
    assert parsed["registrar"].startswith("ODTÜ GELİŞTİRME VAKFI")  # the accredited registrar, not the host
    assert parsed["statuses"] == ["Active"]
    assert parsed["registrant"] == "Example Registrant A.S."  # "Hidden upon user request" lines are skipped
    assert parsed["transfer_status"] == "The domain is LOCKED to transfer."
    assert whois.transfer_locked(parsed["statuses"], parsed["transfer_status"]) is True
    assert parsed["registration_date"] == "2000-07-06"
    assert parsed["expiration_date"] == "2027-07-05"
    assert parsed["nameservers"] == ["ns1.natrohost.com", "ns2.natrohost.com"]


def test_parse_verisign_record():
    parsed = whois.parse_whois_record(read("whois", "verisign.txt"))
    assert parsed["registrar"].startswith("RESERVED-Internet")
    assert parsed["registration_date"] == "1995-08-14"
    assert parsed["expiration_date"] == "2025-08-13"
    assert parsed["nameservers"] == ["a.iana-servers.net", "b.iana-servers.net"]


def test_parse_date_formats():
    assert whois.parse_date("2027-Jul-05.") == "2027-07-05"
    assert whois.parse_date("05-Jul-2027") == "2027-07-05"
    assert whois.parse_date("garbage") is None


# ---- sitemaps ---------------------------------------------------------------------------------------------------

def test_parse_sitemap_tolerates_doctype():
    parsed = sitemaps.parse_sitemap(read("sitemaps", "urlset_doctype.xml").encode())
    assert parsed["kind"] == "urlset"
    assert parsed["entries"] == [("https://example.com/", "2026-01-02"), ("https://example.com/about", None)]


def test_parse_sitemap_index_and_gzip():
    body = read("sitemaps", "index.xml").encode()
    assert [loc for loc, _ in sitemaps.parse_sitemap(gzip.compress(body))["entries"]] == [
        "https://example.com/sm-1.xml", "https://example.com/sm-2.xml"]


def test_entity_expansion_never_happens():
    # The DTD is stripped before parsing, so entities stay undefined text instead of expanding.
    parsed = sitemaps.parse_sitemap(read("sitemaps", "bomb.xml").encode())
    assert all("aaaa" not in loc for loc, _ in parsed["entries"])


def test_parse_robots():
    info = sitemaps.parse_robots("User-agent: *\nDisallow: /a\nDisallow:\nAllow: /b # c\nCrawl-delay: 2\nSitemap: https://x.com/s.xml\n")
    assert info == {"sitemap_directives": ["https://x.com/s.xml"], "allow_rule_count": 1, "disallow_rule_count": 1, "crawl_delay": 2.0}


def test_profile_sitemaps_follows_index(monkeypatch):
    from site_profile.http import Response

    bodies = {
        "https://example.com/robots.txt": b"Sitemap: https://example.com/index.xml\nDisallow: /x\n",
        "https://example.com/index.xml": read("sitemaps", "index.xml").encode(),
        "https://example.com/sm-1.xml": read("sitemaps", "urlset_doctype.xml").encode(),
        "https://example.com/sm-2.xml": read("sitemaps", "urlset_doctype.xml").encode(),
    }

    def fake_fetch(url, **options):
        return Response(url=url, status=200, body=bodies[url]) if url in bodies else Response(url=url, status=404)

    monkeypatch.setattr(sitemaps, "fetch", fake_fetch)
    section, errors = sitemaps.profile_sitemaps("https://example.com/", 5)
    assert errors == []
    assert section["sitemaps"]["total_urls"] == 4
    assert [f["kind"] for f in section["sitemaps"]["files"]] == ["sitemapindex", "urlset", "urlset"]
    assert section["robots"]["disallow_rule_count"] == 1


# ---- page -------------------------------------------------------------------------------------------------------

@pytest.fixture(scope="module")
def agency():
    return page_mod.extract(read("html", "agency.html"), "https://www.forazeytin.com.tr/", "text/html")


def test_page_metadata(agency):
    page, _, _ = agency
    assert page["title"].startswith("Fora Zeytin |")
    assert page["html_language"] == "tr" and page["noindex"] is True
    assert page["canonical_url"] == "https://www.forazeytin.com.tr/"
    assert page["favicon_url"] == "https://www.forazeytin.com.tr/img/fav.png"
    assert page["open_graph"]["image"] == "https://www.forazeytin.com.tr/img/og.jpg"
    assert page["twitter"]["card"] == "summary_large_image"
    assert [h["lang"] for h in page["hreflang"]] == ["en", "tr"]
    assert page["h1_count"] == 1 and [h["level"] for h in page["headings"]] == [1, 2, 2, 3]
    assert (page["image_count"], page["images_missing_alt"], page["form_count"]) == (2, 1, 1)
    assert page["links"] == {"internal": 2, "external": 4, "nofollow": 1}
    assert page["structured_data_types"] == ["LocalBusiness", "PostalAddress"]
    assert page["consent_tool"] == "OneTrust"


def test_contacts(agency):
    _, contacts, _ = agency
    assert contacts["emails"] == ["info@forazeytin.com.tr", "sales@forazeytin.com.tr"]
    assert "+902125551122" in contacts["phones"] and "+902125553344" in contacts["phones"]
    assert contacts["address"].startswith("Atatürk Cad. 1")
    assert [(s["network"]) for s in contacts["socials"]] == ["instagram"]  # the share link is ignored
    assert contacts["whatsapp"] == ["https://wa.me/905551112233"]


def test_wordpress_hints(agency):
    _, _, wp = agency
    assert wp == {"detected": True, "version": "6.6.1", "theme": "astra", "plugins": ["elementor"]}


def test_page_shape_is_stable_for_empty_html():
    page, contacts, wp = page_mod.extract("   ", "https://x.com/", "text/html")
    assert set(page) == set(page_mod.empty_page()) and set(contacts) == set(page_mod.empty_contacts())
    assert page["parseable_html"] is False and wp["detected"] is False


# ---- tech -------------------------------------------------------------------------------------------------------

def test_technology_detection_and_wix_false_positive():
    html = '<script src="https://static.parastorage.com/x.js"></script><div data-reactroot></div> react-dom'
    page = {"external_script_hosts": ["static.parastorage.com"], "script_count": 1, "consent_tool": None}
    found = {(t["category"], t["name"]) for t in tech.detect_technologies(html, {"server": "Pepyaka"}, [], page, {})}
    assert ("CMS", "Wix") in found
    assert ("Framework", "React") not in found  # bundled by the builder, not the site's own choice


def test_technology_detection_headers_and_analytics():
    html = "<script>gtag('config','G-ABCDE12345')</script> /wp-content/themes/x/"
    headers = {"server": "nginx", "x-powered-by": "PHP/7.4.33", "cf-ray": "abc"}
    page = {"external_script_hosts": [], "script_count": 1, "consent_tool": "Cookiebot"}
    found = {(t["category"], t["name"]): t for t in tech.detect_technologies(html, headers, [{"name": "PHPSESSID"}], page, {})}
    assert found[("CMS", "WordPress")]["confidence"] == "high"
    assert ("Likely backend language", "PHP") in found
    assert ("Hosting / edge", "Cloudflare") in found
    assert ("Analytics", "Google Analytics 4") in found
    assert ("Consent", "Cookiebot") in found


def test_lenient_sitemap_parse_for_invalid_xml():
    body = b'<?xml version="1.0"?><urlset><url><loc>https://x.com/a?b=1&c=2</loc><lastmod>2026-01-01</lastmod></url><url><loc>https://x.com/b</loc></url>'
    parsed = sitemaps.parse_sitemap(body)  # unescaped & and a missing closing tag
    assert parsed["lenient"] is True
    assert parsed["entries"] == [("https://x.com/a?b=1&c=2", "2026-01-01"), ("https://x.com/b", None)]
    with pytest.raises(ValueError):
        sitemaps.parse_sitemap(b"<html>nope &</html>")


def test_tr_registration_ignores_database_timestamp(monkeypatch):
    from site_profile import registration

    monkeypatch.setattr(registration, "json_get", lambda *a, **k: (None, "offline"))
    monkeypatch.setattr(whois, "whois_query", lambda *a, **k: read("whois", "trabis.txt"))
    result = registration.profile_registration(domain_parts("forazeytin.com.tr"), 5)
    assert result["last_updated"] is None and result["expiration_date"] == "2027-07-05"
    assert result["registrant"] == "Example Registrant A.S." and result["transfer_locked"] is True
    assert result["lookup_method"] == "WHOIS" and result["error"] is None


def test_transfer_lock_detection():
    assert whois.transfer_locked(["clientTransferProhibited https://icann.org/epp#clientTransferProhibited"]) is True
    assert whois.transfer_locked(["client transfer prohibited"]) is True
    assert whois.transfer_locked(["ok"]) is False
    assert whois.transfer_locked([]) is None
    assert whois.transfer_locked(["Active"]) is None
    assert whois.transfer_locked(["Active"], "The domain is NOT LOCKED to transfer.") is False
    assert whois.transfer_locked(["Active"], "The domain is unlocked") is False


def test_registrant_from_rdap_and_generic_whois():
    from site_profile import registration

    rdap = {"entities": [{"roles": ["registrant"], "vcardArray": ["vcard", [["fn", {}, "text", "REDACTED FOR PRIVACY"], ["org", {}, "text", "Acme Ltd"]]]}]}
    assert registration.registrant_from_rdap(rdap) == "Acme Ltd"
    assert whois.parse_whois_record("Registrant Organization: Acme GmbH\nRegistrant Name: REDACTED FOR PRIVACY\n")["registrant"] == "Acme GmbH"


def test_noscript_images_do_not_break_extraction():
    html = '<html><body><img src="a.jpg" alt="a"><noscript><img src="b.jpg"></noscript><img src="c.jpg"></body></html>'
    page, _, _ = page_mod.extract(html, "https://x.com/", "text/html")
    assert (page["image_count"], page["images_missing_alt"]) == (3, 2) and page["error"] is None


def test_reserved_tld_skips_registration_lookup():
    from site_profile import registration

    result = registration.profile_registration(domain_parts("fora-react.test"), 5)
    assert result["error"] is None and "Reserved or local" in result["note"]


def test_unreadable_sitemap_is_a_warning_with_a_snippet(monkeypatch):
    from site_profile.http import Response

    def fake_fetch(url, **options):
        if url.endswith("robots.txt"):
            return Response(url=url, status=404)
        return Response(url=url, status=200, body=b"<html><body>Oops, a PHP error page</body></html>")

    monkeypatch.setattr(sitemaps, "fetch", fake_fetch)
    section, errors = sitemaps.profile_sitemaps("https://example.com/", 5)
    assert errors == []
    assert "PHP error page" in section["sitemaps"]["files"][0]["warning"]


# ---- design age clues ---------------------------------------------------------------------------------------------

def test_design_clues_from_wordpress_uploads_and_copyright():
    from site_profile import design

    html = (
        '<img src="/wp-content/uploads/2018/08/logo.jpg"><img srcset="/wp-content/uploads/2018/08/a.jpg 1x, /wp-content/uploads/2023/11/b.jpg 2x">'
        '<link rel="stylesheet" href="/wp-content/themes/x/style.css?ver=1535760000"><footer>© 2018-2024 Fora</footer>'
        '<script>var x = "/blog/2015/01/post-slug/"</script>'
    )
    clues = design._from_html(html)
    by_kind = {(c["kind"], c["label"].split()[0]): c for c in clues}
    assert by_kind[("wp-uploads", "Oldest")]["date"] == "2018-08"
    assert by_kind[("wp-uploads", "Newest")]["date"] == "2023-11"
    assert by_kind[("version-stamp", "Oldest")]["date"] == "2018-09-01"
    assert by_kind[("copyright", "Copyright")]["date"] == "2018"
    assert not any(c["date"].startswith("2015") for c in clues), "blog post URLs are not assets"
    summary = design.summarize(clues)
    assert (summary["estimated_from"], summary["latest_asset"], summary["confidence"]) == ("2018-08", "2023-11", "medium")


def test_design_dated_paths_outside_wordpress_and_context():
    from site_profile import design

    clues = design._from_html('<img src="/images/2019/05/hero.webp"><img src="/sites/default/files/2020-03/x.png">')
    assert [c["date"] for c in clues] == ["2019-05", "2020-03"] or {c["date"] for c in clues} == {"2019-05", "2020-03"}
    merged = design.add_context(design.summarize(clues), "2009-03-01T00:00:00+00:00", "2000-07-06", ["2021-02-03T10:00:00Z", ""])
    kinds = [c["kind"] for c in merged["clues"]]
    assert {"archive", "domain", "sitemap"} <= set(kinds)
    assert merged["estimated_from"] == "2019-05", "archive and domain age are context, not design dates"
    assert design.summarize([])["estimated_from"] is None


def test_last_modified_clues_skip_fresh_and_missing_headers(monkeypatch):
    from site_profile import design
    from site_profile.http import Response

    headers = {
        "https://x.com/theme/style.css": {"last-modified": "Wed, 03 Oct 2018 10:00:00 GMT"},
        "https://x.com/app.js": {},
        "https://x.com/fresh.js": {"last-modified": email_now()},
    }
    monkeypatch.setattr(design, "fetch_once", lambda url, **o: Response(url=url, status=200, headers=headers[url]))
    clues = design._last_modified(list(headers), 5, False)
    assert [(c["date"], c["strength"]) for c in clues] == [("2018-10-03", "strong")]


def email_now() -> str:
    import email.utils

    return email.utils.formatdate(usegmt=True)
