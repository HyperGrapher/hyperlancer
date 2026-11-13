"""Contact channels: E.164 phones, WhatsApp links, email kinds, contact pages and people."""

from bs4 import BeautifulSoup

from site_profile import channels


def test_phone_numbers_are_normalised_and_typed():
    assert channels.classify_phone("0532 123 45 67", "TR")["e164"] == "+905321234567"
    assert channels.classify_phone("0532 123 45 67", "TR")["type"] == "mobile"
    fixed = channels.classify_phone("+90 312 468 12 34", None)
    assert fixed["e164"] == "+903124681234" and fixed["type"] == "fixed" and fixed["country"] == "TR"
    assert channels.classify_phone("0090 532 123 45 67", None)["e164"] == "+905321234567"
    assert channels.classify_phone("12345", "TR") is None
    assert channels.classify_phone("not a number", "TR") is None


def test_default_region_from_the_domain():
    assert channels.default_region("firma.com.tr") == "TR"
    assert channels.default_region("example.com") is None


def test_whatsapp_links():
    assert channels.parse_whatsapp_link("https://wa.me/905321234567?text=Merhaba%20d%C3%BCnya") == \
        {"number": "+905321234567", "text": "Merhaba dünya"}
    assert channels.parse_whatsapp_link("https://api.whatsapp.com/send?phone=905321234567&text=Hi")["number"] == "+905321234567"
    assert channels.parse_whatsapp_link("whatsapp://send?phone=+90 532 123 45 67")["number"] == "+905321234567"
    assert channels.parse_whatsapp_link("https://wa.me/message/ABCDEF") is None  # a business link has no number


def test_email_kinds():
    assert channels.classify_email("info@firma.com") == "generic"
    assert channels.classify_email("iletisim@firma.com") == "generic"
    assert channels.classify_email("info2@firma.com") == "generic"
    assert channels.classify_email("ayse.demir@firma.com") == "personal"


def test_widgets_and_people():
    assert "Joinchat" in channels.widgets_in('<div class="joinchat joinchat--right"></div>')
    soup = BeautifulSoup('<script type="application/ld+json">{"@graph":[{"@type":"Person","name":"Ayşe Demir","jobTitle":"Owner"}]}</script>', "html.parser")
    assert channels.people_in(soup) == [{"name": "Ayşe Demir", "role": "Owner"}]


def test_contact_pages_are_found_from_links_only():
    html = ('<a href="/iletisim">İletişim</a><a href="/hakkımızda">About</a><a href="/urunler">Products</a>'
            '<a href="https://other.com/contact">Elsewhere</a><a href="/">Home</a><a href="/contact#form">Contact</a>')
    urls = channels.contact_page_urls(BeautifulSoup(html, "html.parser"), "https://www.firma.com.tr/")
    assert urls == ["https://www.firma.com.tr/iletisim", "https://www.firma.com.tr/hakkımızda", "https://www.firma.com.tr/contact"]


def test_build_merges_pages_and_marks_likely_whatsapp():
    pages = [
        {"url": "https://firma.com.tr/", "phones": ["+903124681234", "05321234567"], "whatsapp": [], "emails": ["info@firma.com.tr"], "widgets": [], "people": []},
        {"url": "https://firma.com.tr/iletisim", "phones": ["+903124681234"], "whatsapp": ["https://wa.me/905441112233"],
         "emails": ["info@firma.com.tr", "ayse@firma.com.tr"], "widgets": ["Joinchat"], "people": [{"name": "Ayşe", "role": "Owner"}]},
    ]
    result = channels.build(pages, "firma.com.tr")
    assert {p["e164"] for p in result["phones"]} == {"+903124681234", "+905321234567"}
    fixed = next(p for p in result["phones"] if p["e164"] == "+903124681234")
    assert fixed["pages"] == ["https://firma.com.tr/", "https://firma.com.tr/iletisim"]
    status = {w["number"]: w["status"] for w in result["whatsapp"]}
    assert status == {"+905441112233": "confirmed", "+905321234567": "likely"}  # the mobile number without a link is only likely
    assert [e["kind"] for e in result["emails"]] == ["generic", "personal"]
    assert result["widgets"] == ["Joinchat"] and result["people"][0]["name"] == "Ayşe"
