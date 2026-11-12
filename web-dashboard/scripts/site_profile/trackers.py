"""Ad and analytics trackers found in the page HTML (the static pass).

Tags that Google Tag Manager injects at run time are not in the HTML; they need the rendered
pass (a later job). When a Tag Manager container is found, the result says so.
"""

from __future__ import annotations

import json
import re
from functools import lru_cache
from pathlib import Path
from typing import Any

RULES_PATH = Path(__file__).with_name("data") / "trackers.json"
MAX_IDS = 5


@lru_cache(maxsize=1)
def load_rules() -> list[dict[str, Any]]:
    data = json.loads(RULES_PATH.read_text(encoding="utf-8"))
    rules = []
    for rule in data["rules"]:
        rules.append({**rule, "_patterns": [re.compile(p, re.I) for p in rule["patterns"]],
                      "_id": re.compile(rule["id"], re.I) if rule.get("id") else None})
    return rules


def detect(html: str) -> dict[str, Any]:
    items: list[dict[str, Any]] = []
    for rule in load_rules():
        hits = [m for p in rule["_patterns"] for m in [p.search(html)] if m]
        if not hits:
            continue
        ids: list[str] = []
        if rule["_id"] is not None:
            for match in rule["_id"].finditer(html):
                value = match.group(1) if match.groups() and match.group(1) else match.group(0)
                if value and value not in ids:
                    ids.append(value)
        items.append({
            "vendor": rule["vendor"], "product": rule["product"], "kind": rule["kind"], "status": rule["status"],
            "ids": ids[:MAX_IDS], "evidence": hits[0].group(0)[:120], "source": "static", "note": rule.get("note"),
        })
    ads = [i for i in items if i["kind"] == "ads"]
    platforms = sorted({i["vendor"] for i in ads})
    products = {i["product"] for i in items}
    has_ga4 = "Google Analytics 4" in products
    has_ua = "Universal Analytics" in products
    return {
        "items": items,
        "runsAds": bool(ads),
        "adPlatforms": platforms,
        # A UA- tag alone means nobody is looking at the numbers any more.
        "staleAnalytics": has_ua and not has_ga4,
        "hasTagManager": "Google Tag Manager" in products,
    }


def empty() -> dict[str, Any]:
    return {"items": [], "runsAds": False, "adPlatforms": [], "staleAnalytics": False, "hasTagManager": False}
