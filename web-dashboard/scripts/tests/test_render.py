import json
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from site_profile import trackers
from .crawl_server import html, serve

CHROMIUM = "/opt/pw-browsers/chromium"
pytestmark = pytest.mark.skipif(not Path(CHROMIUM).exists() and not shutil.which("google-chrome"), reason="no browser")


def test_detect_requests_marks_rendered_source():
    result = trackers.detect_requests(["https://www.googletagmanager.com/gtm.js?id=GTM-ABC123"])
    assert result["hasTagManager"]
    assert all(item["source"] == "rendered" for item in result["items"])


def test_render_writes_screenshots_and_report(tmp_path):
    script = '<script>fetch("/late.js").catch(()=>{})</script>'
    routes = {"/": html("<h1>Hello</h1>" + script, title="Local page"), "/late.js": (200, {"Content-Type": "text/javascript"}, b"")}
    with serve(routes) as (base, hits):
        out = tmp_path / "render"
        code = subprocess.run([sys.executable, "-m", "site_render", base + "/", "--out", str(out), "--browser", CHROMIUM,
                               "--allow-private"], capture_output=True, text=True, timeout=120).returncode
    assert code == 0
    report = json.loads((out / "render.json").read_text())
    assert report["title"] == "Local page"
    assert report["requestCount"] >= 2
    for name in ("desktop.jpg", "mobile.jpg", "fullpage.jpg"):
        assert (out / name).stat().st_size > 500


def test_render_refuses_private_target(tmp_path):
    result = subprocess.run([sys.executable, "-m", "site_render", "http://127.0.0.1:9/", "--out", str(tmp_path)],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 4
