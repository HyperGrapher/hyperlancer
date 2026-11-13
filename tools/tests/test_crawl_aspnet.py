"""ASP.NET WebForms sites wrap the whole page in one <form>; the crawler must not drop it."""

from types import SimpleNamespace

from site_crawl.extract import PageExtractor
from site_crawl.options import Options


class FakeAssets:
    ignore = staticmethod(lambda url: False)

    def __init__(self):
        self.calls: list[list[str]] = []

    def save(self, candidates, kind, page_id, alt=""):
        self.calls.append(list(candidates))
        return SimpleNamespace(id=f"a{len(self.calls)}", file=f"images/{len(self.calls)}.jpg")


PAGE = """<html><head><title>Home</title></head><body>
<form method="post" action="./" id="form1"><input type="hidden" name="__VIEWSTATE" value="x">
<div class="body main">
  <header><nav><a href="/a">Menu A</a></nav><div>Logo</div></header>
  <div class="main"><section><h1>About us</h1></section><article><p>We design rooms.</p>
    <img src="/blank.gif" data-src="/real.jpg" alt="slide">
    <div class="slideshowPlugin"><ul><li><a href="/big1.jpg"><img src="/thumb1.jpg" width="50"></a></li></ul></div>
  </article></div>
  <footer><p>Call 0342 909 85 89</p><nav><a href="/x">Footer menu</a></nav></footer>
</div></form>
<form action="/search"><label>Search</label><input name="q"></form>
</body></html>"""


def extract(html: str, with_chrome: bool = False):
    assets = FakeAssets()
    out = PageExtractor(Options.from_params({"startUrls": ["https://example.test/"]}, __import__("pathlib").Path("/tmp/unused")), assets,
                        "example.test").extract(html, "https://example.test/", "p1", "../", with_chrome=with_chrome)
    return out, assets


def test_page_wrapping_form_is_kept_but_small_forms_are_dropped():
    out, _ = extract(PAGE)
    assert "We design rooms." in out.body
    assert "About us" in out.body
    assert "Search" not in out.body


def test_lazy_data_src_wins_over_a_blank_placeholder():
    _, assets = extract(PAGE)
    flattened = [c for call in assets.calls for c in call]
    assert "https://example.test/real.jpg" in flattened
    assert not any("blank.gif" in c for c in flattened[:1])


def test_header_and_footer_text_is_returned_once_without_menus():
    out, _ = extract(PAGE, with_chrome=True)
    assert "Call 0342 909 85 89" in out.chrome
    assert "Footer menu" not in out.chrome and "Menu A" not in out.chrome
    assert "Call 0342" not in out.body


def test_meta_refresh_target_is_followed():
    out, _ = extract('<html><meta http-equiv="refresh" content="0; url=tr/" /></html>')
    assert ("page", "https://example.test/tr/") in out.discovered
