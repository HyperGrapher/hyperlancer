"""Wix support in the content crawler: full-size images, gallery items outside the markup, title-only headings."""

import json
from types import SimpleNamespace

from bs4 import BeautifulSoup

from site_crawl import wix
from site_crawl.extract import PageExtractor
from site_crawl.options import Options
from site_crawl.render import tidy

PREVIEW = "https://static.wixstatic.com/media/248d21_aaaa~mv2.jpg/v1/fill/w_514,h_386,q_90,enc_avif,quality_auto/248d21_aaaa~mv2.jpg"


def test_original_url_removes_the_resize_part():
    assert wix.original_url(PREVIEW) == "https://static.wixstatic.com/media/248d21_aaaa~mv2.jpg"
    assert wix.original_url("https://static.wixstatic.com/media/248d21_aaaa~mv2.jpg/v1/fit/w_2500,h_1330,al_c/x.jpg") == \
        "https://static.wixstatic.com/media/248d21_aaaa~mv2.jpg"
    assert wix.original_url("https://static.wixstatic.com/media/248d21_aaaa%7Emv2.png/v1/fill/w_32%2Ch_32/x.png") == \
        "https://static.wixstatic.com/media/248d21_aaaa%7Emv2.png"
    # Already the uploaded file, another host, or not a media path: nothing to do.
    assert wix.original_url("https://static.wixstatic.com/media/248d21_aaaa~mv2.jpg") is None
    assert wix.original_url("https://example.com/media/a.jpg/v1/fill/w_1,h_1/a.jpg") is None
    assert wix.original_url("https://static.wixstatic.com/shapes/abc.svg") is None
    assert wix.media_id(PREVIEW) == "248d21_aaaa~mv2.jpg"


class FakeAssets:
    ignore = staticmethod(lambda url: False)

    def __init__(self):
        self.calls: list[list[str]] = []

    def save(self, candidates, kind, page_id, alt=""):
        self.calls.append(list(candidates))
        number = len(self.calls)
        return SimpleNamespace(id=f"a{number}", file=f"images/{number}.jpg")


GALLERY_DATA = {
    "appsWarmupData": {"app": {"comp-gal1_galleryData": {"totalItemsCount": 50, "items": [
        {"mediaUrl": "248d21_aaaa~mv2.jpg", "metaData": {"type": "image", "width": 4000, "height": 3000, "alt": "Shown"}},
        {"mediaUrl": "248d21_bbbb~mv2.jpg", "metaData": {"type": "image", "width": 4000, "height": 3000, "alt": "Not in the markup"}},
        {"mediaUrl": "248d21_vid1", "metaData": {"type": "video", "posters": [
            {"url": "248d21_vid1f000.jpg", "width": 3840, "height": 2160}, {"url": "248d21_vid1f001.jpg", "width": 640, "height": 352}]}},
    ]}, "comp-other_galleryData": {"totalItemsCount": 1, "items": [
        {"mediaUrl": "248d21_cccc~mv2.jpg", "metaData": {"type": "image"}}]}}},
}


def wix_page(body: str) -> str:
    return (
        '<html lang="tr"><head><meta name="generator" content="Wix.com Website Builder"/><title>Galeri</title></head><body>'
        '<header id="comp-other">menu</header>'
        f'<main>{body}</main><script type="application/json" id="wix-warmup-data">{json.dumps(GALLERY_DATA)}</script></body></html>'
    )


def extractor(tmp_path):
    opts = Options.from_params({"startUrls": ["https://example.test/"]}, tmp_path)
    assets = FakeAssets()
    return PageExtractor(opts, assets, "example.test"), assets


def test_gallery_items_outside_the_markup_and_hidden_ones_are_crawled(tmp_path):
    page = wix_page(
        '<div id="comp-gal1">'
        f'<div aria-hidden="false"><picture><img src="{PREVIEW}" alt="Shown"></picture></div>'
        # A video whose poster the page shows as another frame than the largest one in the data.
        '<div aria-hidden="true"><picture><img src="https://static.wixstatic.com/media/248d21_vid1f003.jpg/v1/fill/w_9,h_9/x.jpg"></picture></div>'
        '</div>'
    )
    extract, assets = extractor(tmp_path)
    result = extract.extract(page, "https://example.test/galeri", "p1", "../")
    originals = [calls[0] for calls in assets.calls]
    assert "https://static.wixstatic.com/media/248d21_aaaa~mv2.jpg" in originals  # full size first, preview as fallback
    assert assets.calls[0][-1] == PREVIEW
    assert "https://static.wixstatic.com/media/248d21_bbbb~mv2.jpg" in originals  # only in the JSON
    assert not any("cccc" in url for calls in assets.calls for url in calls)  # another component's gallery is left alone
    assert not any("vid1f000" in url for url in originals)  # the video is already shown through its f003 poster
    assert len(assets.calls) == 3  # the shown image, the hidden poster, the JSON-only image
    assert any("1" in note and "of 50 items" in note for note in result.notes)


def test_aria_hidden_still_hides_blocks_on_other_sites(tmp_path):
    extract, assets = extractor(tmp_path)
    page = ('<html><head><title>Plain</title></head><body><main><p>Visible text here</p>'
            '<div aria-hidden="true"><img src="/slider-clone.jpg"></div></main></body></html>')
    extract.extract(page, "https://example.test/", "p1", "../")
    assert assets.calls == []


def test_title_only_headings_are_kept_when_they_are_siblings():
    blocks = ["# Page", "## Our features", "### Technology", "### Green", "### Quality", "Some text", "## Contact", "### Empty", "## Next"]
    kept = tidy(blocks)
    assert "### Technology" in kept and "### Green" in kept and "### Quality" in kept
    assert "### Empty" not in kept  # followed by a higher-level heading: introduces nothing


class FakeResponse:
    def __init__(self, payload=None, status=200):
        self.status = status
        self.body = json.dumps(payload).encode() if payload is not None else b""
        self.error = None

    @property
    def ok(self):
        return self.status == 200


class FakeFetcher:
    """Answers the two calls the Pro Gallery paging makes, and records them."""

    def __init__(self, total=230, fail_after=None):
        self.total, self.fail_after, self.calls = total, fail_after, []

    def get(self, url, max_bytes=0, headers=None):
        self.calls.append((url, headers))
        if url.endswith("/_api/v1/access-tokens"):
            return FakeResponse({"apps": {wix.SITE_APP: {"instance": "TOKEN"}}})
        offset = int(url.split("offset=")[1].split("&")[0])
        if self.fail_after is not None and offset >= self.fail_after:
            return FakeResponse(status=500)
        items = []
        for number in range(offset, min(offset + wix.PAGE_SIZE, self.total)):
            if number == 3:
                items.append({"dataType": "Video", "mediaUrl": "https://video.wixstatic.com/video/vid3/480p/mp4/file.mp4",
                              "videoMetadata": {"posters": [{"url": "https://static.wixstatic.com/media/vid3f000.jpg", "width": 3840, "height": 2160}]}})
            else:
                items.append({"dataType": "Photo", "mediaUrl": f"https://static.wixstatic.com/media/248d21_{number:04d}~mv2.jpg",
                              "alt": f"Photo {number}", "photoMetadata": {"width": 4000, "height": 3000}})
        return FakeResponse({"gallery": {"totalItemsCount": self.total, "items": items}})


def test_every_gallery_item_is_fetched_page_by_page_with_the_site_token():
    fetcher = FakeFetcher(total=230)
    gallery = wix.Gallery("comp-gal1", [], 230, "gid-1")
    items = wix.fetch_all_items(fetcher, "https://example.test", gallery)
    assert len(items) == 230
    assert items[3].is_video_poster and items[3].url.endswith("vid3f000.jpg")  # a video becomes its poster
    pages = [call for call in fetcher.calls if "offset=" in call[0]]
    assert [p[0].split("offset=")[1].split("&")[0] for p in pages] == ["0", "100", "200"]
    assert all(headers == {"Authorization": "TOKEN"} for _, headers in pages)


def test_gallery_paging_keeps_what_it_got_when_the_service_fails():
    items = wix.fetch_all_items(FakeFetcher(total=230, fail_after=100), "https://example.test", wix.Gallery("c", [], 230, "gid"))
    assert len(items) == 100
    assert wix.fetch_all_items(FakeFetcher(), "https://example.test", wix.Gallery("c", [], 5, "")) is None  # no gallery id


def test_extractor_uses_the_paged_items_instead_of_the_few_in_the_page(tmp_path):
    GALLERY_DATA["appsWarmupData"]["app"]["comp-gal1_appSettings"] = {"galleryId": "gid-1"}
    extract, assets = extractor(tmp_path)
    assets.fetcher = FakeFetcher(total=50)
    result = extract.extract(wix_page(f'<div id="comp-gal1"><img src="{PREVIEW}"></div>'), "https://example.test/galeri", "p1", "../")
    assert len(assets.calls) == 51  # the 50 gallery items plus the one image that is only in the page markup
    assert not result.notes  # everything was found
