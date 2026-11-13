import io
import json

import pytest

from site_tools import netguard
from site_tools.cancel import Cancelled, CancelToken
from site_tools.events import Emitter
from site_tools.spec import SpecError, load_spec


@pytest.mark.parametrize(
    "ip,blocked",
    [
        ("127.0.0.1", True), ("10.1.2.3", True), ("192.168.1.1", True), ("172.16.0.5", True),
        ("169.254.169.254", True), ("100.64.0.1", True), ("0.0.0.0", True), ("224.0.0.1", True),
        ("::1", True), ("fd00::1", True), ("fe80::1", True), ("::ffff:127.0.0.1", True),
        ("93.184.216.34", False), ("1.1.1.1", False), ("2606:4700:4700::1111", False),
    ],
)
def test_is_blocked_ip(ip, blocked):
    assert netguard.is_blocked_ip(ip) is blocked


def test_check_url_blocks_private_literals_and_ports():
    with pytest.raises(netguard.BlockedTarget) as exc:
        netguard.check_url("http://192.168.1.1/")
    assert exc.value.reason == "private_network"
    with pytest.raises(netguard.BlockedTarget) as exc:
        netguard.check_url("https://93.184.216.34:22/")
    assert exc.value.reason == "port"
    netguard.check_url("https://93.184.216.34/")
    netguard.check_url("http://192.168.1.1/", allow_private=True)


def test_check_url_checks_resolved_addresses(monkeypatch):
    monkeypatch.setattr(netguard, "resolve", lambda host: ["93.184.216.34", "10.0.0.8"])
    with pytest.raises(netguard.BlockedTarget):
        netguard.check_url("https://rebind.example/")
    monkeypatch.setattr(netguard, "resolve", lambda host: [])
    netguard.check_url("https://nxdomain.example/")  # DNS failure is reported by the fetch, not here


def test_emitter_writes_one_json_line_per_event():
    out = io.StringIO()
    emitter = Emitter(stream=out)
    emitter.progress(2, 8, "dns")
    emitter.log("warn", "Wayback ünicode")
    lines = out.getvalue().splitlines()
    assert json.loads(lines[0]) == {"type": "progress", "current": 2, "total": 8, "label": "dns"}
    assert json.loads(lines[1])["message"] == "Wayback ünicode"
    Emitter(enabled=False, stream=out).log("info", "dropped")
    assert len(out.getvalue().splitlines()) == 2


def test_cancel_token_file(tmp_path):
    flag = tmp_path / "cancel"
    token = CancelToken(flag)
    token.check()
    flag.touch()
    assert token.cancelled
    with pytest.raises(Cancelled):
        token.check()


def test_load_spec(tmp_path):
    good = tmp_path / "spec.json"
    good.write_text(json.dumps({"params": {"url": "example.com"}}))
    assert load_spec(good)["params"]["url"] == "example.com"
    bad = tmp_path / "bad.json"
    bad.write_text("[1]")
    with pytest.raises(SpecError):
        load_spec(bad)
    with pytest.raises(SpecError):
        load_spec(tmp_path / "missing.json")
