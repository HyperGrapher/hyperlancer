import json
from pathlib import Path

import pytest

from site_crawl import assets as assets_mod
from site_crawl import urls
from site_crawl.cli import main
from site_crawl.output import rewrite_links
from site_tools import exitcodes
from tests.crawl_server import PNG_1X1, html, js, png, serve


def site_routes() -> dict:
    logo = (200, {"Content-Type": "image/png"}, PNG_1X1)
    return {
        "/robots.txt": (200, {"Content-Type": "text/plain"}, b"User-agent: *\nDisallow: /private\n"),
        "/sitemap.xml": (200, {"Content-Type": "application/xml"}, b'<?xml version="1.0"?><urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9"><url><loc>/hidden</loc></url></urlset>'),
        "/": html(
            '<header><a href="/">Logo</a><nav><a href="/about">Nav about</a></nav></header>'
            '<main><h1>Fora Zeytin</h1><p>Welcome to the <a href="/about#team">about page</a> and the '
            '<a href="/products/olive-oil.php?utm_source=x">olive oil</a>. <a href="/missing">Broken</a> '
            '<a href="/private/area">Private</a> <a href="https://elsewhere.example/page">External</a> '
            '<a href="/files/brochure.pdf">Brochure</a></p><img src="/img/logo.png" alt="Logo"><img src="/img/logo-copy.png" alt="Logo again">'
            '<h2>Products</h2></main><footer>© 2018 Fora</footer>',
            title="Home",
        ),
        "/about": html('<main><h1>About us</h1><p>Back to <a href="/">home</a>.</p><img src="/img/logo.png" alt="Same logo"></main>', title="About"),
        "/products/olive-oil.php": html("<main><h1>Olive oil</h1><p>Cold pressed.</p></main>", title="Olive oil"),
        "/hidden": html("<main><h1>Hidden</h1><p>Only in the sitemap.</p></main>", title="Hidden"),
        "/private/area": html("<main><h1>Private</h1></main>", title="Private"),
        "/img/logo.png": logo,
        "/img/logo-copy.png": logo,
        "/files/brochure.pdf": (200, {"Content-Type": "application/pdf"}, b"%PDF-1.4 fake"),
    }


@pytest.fixture
def crawl(tmp_path):
    def run(base: str, **params):
        out = tmp_path / "run"
        params = {"startUrls": [base + "/"], "allowPrivate": True, "delay": 0, "maxPages": 50, **params}
        spec = tmp_path / "spec.json"
        spec.write_text(json.dumps({"params": params, "cancelFile": str(tmp_path / "cancel"), "dir": str(tmp_path)}))
        code = main(["--spec", str(spec), "--out", str(out)])
        return code, out

    return run


def read(out: Path, name: str):
    return json.loads((out / name).read_text(encoding="utf-8"))


def test_full_crawl_layout_and_dedup(crawl, capsys):
    with serve(site_routes()) as (base, hits):
        code, out = crawl(base)
    assert code == exitcodes.OK
    manifest = read(out, "manifest.json")
    assert manifest["status"] == "done" and manifest["counts"]["pages"] == 4  # home, about, olive oil, hidden (sitemap)
    assert (out / "pages" / "index.md").is_file() and (out / "pages" / "about.md").is_file()
    assert (out / "pages" / "products--olive-oil.md").is_file(), "path segments joined with --, extension dropped"

    home = (out / "pages" / "index.md").read_text(encoding="utf-8")
    assert home.startswith("---\n") and 'title: "Home"' in home and "source: html" in home
    assert "Nav about" not in home and "© 2018" not in home, "header and footer are removed"
    assert "[about page](about.md#team)" in home, "internal links point at the exported files"
    assert "(products--olive-oil.md)" in home and "utm_source" not in home
    assert "https://elsewhere.example/page" in home, "external links stay absolute"
    assert "![Logo](../images/logo.png)" in home
    assert "[Brochure](../files/brochure.pdf)" in home, "links to downloaded documents point at the local copy"

    about = (out / "pages" / "about.md").read_text(encoding="utf-8")
    assert "[home](index.md)" in about

    assets = read(out, "assets.json")["assets"]
    images = [a for a in assets if a["kind"] == "image"]
    assert len(images) == 1, "identical bytes under two URLs are stored once"
    logo = images[0]
    assert (logo["width"], logo["height"]) == (1, 1) and logo["mime"] == "image/png"
    assert sorted(logo["used_on"]) == ["p1", "p2"] and logo["aliases"] and len(logo["sha256"]) == 64
    assert sorted(path.name for path in (out / "images").iterdir()) == ["logo.png"]
    assert [a["file"] for a in assets if a["kind"] == "file"] == ["files/brochure.pdf"]

    structure = read(out, "structure.json")
    by_url = {p["url"].replace(base, ""): p for p in structure["pages"]}
    assert by_url["/about"]["parent_id"] == "p1" and by_url["/about"]["depth"] == 1
    assert by_url["/"]["headings"][0] == {"level": 1, "text": "Fora Zeytin"}
    assert "p2" in by_url["/"]["links_internal"] and by_url["/"]["images"] == ["a1"]
    assert structure["tree"]["children"][0]["name"] in {"about", "products", "hidden"}

    states = {u["url"].replace(base, ""): (u["state"], u["reason"]) for u in read(out, "urls.json")["urls"]}
    assert states["/missing"] == ("skipped", "HTTP 404")
    assert states["/private/area"] == ("skipped", "disallowed by robots.txt")
    assert states["https://elsewhere.example/page"][0] == "external"
    assert states["/about"][0] == "crawled"
    assert not any(p.name.endswith(".tmp") for p in out.rglob("*")), "atomic writes leave no temp files"

    readme = (out / "README.md").read_text(encoding="utf-8")
    assert "structure.json" in readme and "How to read this folder" in readme
    assert (out / "logs" / "crawl.log").read_text(encoding="utf-8").count("INFO") >= 4
    events = [json.loads(line) for line in capsys.readouterr().out.splitlines()]
    assert {"progress", "stats", "log", "result"} <= {e["type"] for e in events}
    assert events[-1]["type"] == "result" and events[-1]["status"] == "done"


def test_max_pages_stops_the_crawl(crawl):
    with serve(site_routes()) as (base, _):
        code, out = crawl(base, maxPages=2)
    manifest = read(out, "manifest.json")
    assert manifest["counts"]["pages"] == 2 and manifest["stop_reason"] == "max_pages"
    assert code == exitcodes.OK and len(list((out / "pages").glob("*.md"))) == 2


def test_robots_can_be_ignored_when_allowed(crawl):
    with serve(site_routes()) as (base, _):
        code, out = crawl(base, respectRobots=False)
    assert any("private--area" in p.name for p in (out / "pages").iterdir())


def test_exclude_pattern_and_depth(crawl):
    with serve(site_routes()) as (base, _):
        _, out = crawl(base, exclude=["^/products"], maxDepth=0)
    files = sorted(p.name for p in (out / "pages").iterdir())
    assert "products--olive-oil.md" not in files
    # depth 0 follows no links, but the sitemap still seeds /hidden
    assert "about.md" not in files


def test_size_cap_stops_gracefully_and_reports_partial(crawl):
    routes = site_routes()
    routes["/img/logo.png"] = routes["/img/logo-copy.png"] = (200, {"Content-Type": "image/png"}, png(10, 10, filler=40_000))
    with serve(routes) as (base, _):
        code, out = crawl(base, maxTotalMb=0.01)  # ~10 KB
    manifest = read(out, "manifest.json")
    assert manifest["stop_reason"] == "size_cap" and manifest["status"] == "partial" and code == exitcodes.PARTIAL
    assert manifest["counts"]["pages"] >= 1, "what was crawled before the cap is kept and indexed"
    assert read(out, "assets.json")["failures"], "the skipped download is listed"


def test_cancel_leaves_a_consistent_folder(crawl, tmp_path):
    (tmp_path / "cancel").touch()
    with serve(site_routes()) as (base, _):
        code, out = crawl(base)
    assert code == exitcodes.CANCELLED
    manifest = read(out, "manifest.json") if (out / "manifest.json").exists() else None
    assert manifest is None or manifest["status"] in ("canceled", "running")


def test_private_start_url_is_blocked_without_permission(crawl):
    with serve(site_routes()) as (base, hits):
        code, out = crawl(base, allowPrivate=False)
    assert code == exitcodes.BLOCKED and hits == []
    assert read(out, "manifest.json")["status"] == "failed"


def test_unreachable_site_fails(crawl):
    code, out = crawl("http://127.0.0.1:9", timeout=3)
    assert code == exitcodes.FAILED and read(out, "manifest.json")["status"] == "failed"


def test_catch_all_servers_are_detected(crawl):
    same = html("<main><h1>Home</h1><p>Same page for every URL.</p></main>", title="Home")
    routes = {"/": html('<main><h1>Home</h1><p>Same page for every URL.</p><a href="/a">A</a><a href="/b">B</a></main>', title="Home")}
    routes["/a"] = routes["/b"] = routes["/"]
    with serve(routes) as (base, _):
        _, out = crawl(base)
    reasons = [u["reason"] for u in read(out, "urls.json")["urls"] if u["state"] == "skipped"]
    assert read(out, "manifest.json")["counts"]["pages"] == 1 and all("catch-all" in r for r in reasons)


def test_wordpress_api_supplies_content_up_to_the_page_limit(crawl):
    def posts(query):
        count = int(query.get("per_page", ["10"])[0])
        items = [{
            "id": i, "link": f"{base_holder[0]}/blog/post-{i}/", "date": "2024-01-0%d" % i, "modified": "2024-02-01", "title": {"rendered": f"Post {i}"},
            "excerpt": {"rendered": "<p>Short</p>"}, "content": {"rendered": f'<p>Body of post {i}</p><img src="/wp-content/uploads/2024/01/logo-300x200.png" alt="Pic">'},
            "_embedded": {"author": [{"name": "Ali"}], "wp:term": [[{"taxonomy": "category", "name": "News"}], []]},
        } for i in range(1, 6)][:count]
        return 200, {"Content-Type": "application/json", "X-WP-TotalPages": "1"}, json.dumps(items).encode()

    base_holder: list[str] = []
    routes = {
        "/": html("<main><h1>Home</h1></main>", title="Home"),
        "/wp-json/wp/v2/posts": posts,
        "/wp-json/wp/v2/pages": lambda q: (200, {"Content-Type": "application/json", "X-WP-TotalPages": "1"}, b"[]"),
        "/wp-content/uploads/2024/01/logo-300x200.png": (404, {}, b""),
        "/wp-content/uploads/2024/01/logo.png": (200, {"Content-Type": "image/png"}, PNG_1X1),
    }
    with serve(routes) as (base, hits):
        base_holder.append(base)
        code, out = crawl(base, maxPages=3)
    pages = read(out, "structure.json")["pages"]
    assert [p["source"] for p in pages].count("wp-post") == 2 and len(pages) == 3, "the limit counts WordPress items too"
    post = (out / pages[0]["file"]).read_text(encoding="utf-8") if pages[0]["source"] == "wp-post" else (out / next(p["file"] for p in pages if p["source"] == "wp-post")).read_text(encoding="utf-8")
    assert "Body of post" in post and "wp:" in post and '"Ali"' in post and '"News"' in post
    assert any("per_page=100" in h or "per_page=" in h for h in hits if "/wp/v2/posts" in h)
    assert read(out, "assets.json")["assets"][0]["file"] == "images/logo.png", "full-size name tried first, resized URL as fallback"


# ---- units -------------------------------------------------------------------------------------------------------

def test_slugs_and_file_names_are_filesystem_safe():
    assert urls.page_slug("/") == "index"
    assert urls.page_slug("/ürünler/zeytinyağı.php") == "urunler--zeytinyagi"
    assert urls.page_slug("/a/b", "x=1").startswith("a--b--q-")
    long = urls.page_slug("/" + "segment/" * 30)
    assert len(long) <= 100 and urls.page_slug("/con") == "_con"
    assert urls.safe_filename('a<b>:"c".png') == "a_b___c_.png"
    assert urls.safe_filename("nul.txt") == "_nul.txt" and urls.safe_filename("  ..  ") == "file"
    assert len(urls.safe_filename("x" * 300 + ".jpg")) <= 100


def test_rewrite_links_keeps_fragments_and_leaves_unknown_links():
    mapping = {urls.key_of("https://x.com/about"): "pages/about.md", urls.key_of("https://x.com/en/team"): "pages/en/team.md"}
    text = "[a](https://x.com/about#team) [b](https://x.com/about/) [c](https://x.com/other) [d](../images/a.png) [e](https://x.com/en/team)"
    assert rewrite_links(text, "pages/index.md", mapping) == "[a](about.md#team) [b](about.md) [c](https://x.com/other) [d](../images/a.png) [e](en/team.md)"
    assert rewrite_links("[t](https://x.com/about)", "pages/en/team.md", mapping) == "[t](../about.md)"


def test_image_size_sniffing():
    assert assets_mod.image_size(png(640, 480)) == (640, 480)
    assert assets_mod.image_size(b"GIF89a" + (320).to_bytes(2, "little") + (200).to_bytes(2, "little")) == (320, 200)
    jpeg = b"\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00\xff\xc0\x00\x11\x08\x01\xe0\x02\x80\x03\x01\x22\x00\x02\x11\x01\x03\x11\x01"
    assert assets_mod.image_size(jpeg) == (640, 480)
    assert assets_mod.image_size(b"not an image") is None
