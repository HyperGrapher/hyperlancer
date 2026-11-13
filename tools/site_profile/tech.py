"""Heuristic technology detection. Every detection carries its evidence and a confidence level."""

from __future__ import annotations

import json
import re
from typing import Any


def detect_technologies(
    html_text: str,
    headers: dict[str, str],
    cookies: list[dict[str, Any]],
    page: dict[str, Any],
    dns_data: dict[str, Any],
) -> list[dict[str, Any]]:
    html_lower = html_text.lower()
    header_text = "\n".join(f"{key}: {value}" for key, value in headers.items()).lower()
    script_text = "\n".join(page.get("external_script_hosts", [])).lower()
    dns_text = json.dumps(dns_data.get("records", {}), ensure_ascii=False).lower()
    cookie_names = " ".join(cookie["name"].lower() for cookie in cookies)
    evidence_text = "\n".join((html_lower, header_text, script_text, dns_text, cookie_names))
    detections: dict[tuple[str, str], dict[str, Any]] = {}

    def add(category: str, name: str, confidence: str, evidence: str) -> None:
        key = (category, name)
        if key not in detections:
            detections[key] = {
                "category": category,
                "name": name,
                "confidence": confidence,
                "evidence": [],
            }
        detections[key]["evidence"].append(evidence)
        if confidence == "high":
            detections[key]["confidence"] = "high"

    checks = [
        ("CMS", "WordPress", ("/wp-content/", "/wp-includes/", "wp-json", "name=\"generator\" content=\"wordpress")),
        ("CMS", "Wix", ("wixstatic.com", "parastorage.com", "wix.com")),
        ("CMS", "Shopify", ("cdn.shopify.com", "myshopify.com", "shopify.theme")),
        ("CMS", "Webflow", ("webflow.io", "webflow.com", "w-webflow")),
        ("CMS", "Squarespace", ("squarespace.com", "squarespace-cdn.com", "static1.squarespace.com")),
        ("CMS", "Drupal", ("drupal-settings-json", "/sites/default/files/", "drupal.js")),
        ("CMS", "Joomla", ("/media/system/js/", "joomla!", "com_content")),
        ("CMS", "Ghost", ("ghost.io", "/ghost/api/", "content/themes/ghost")),
        ("CMS", "Framer", ("framerusercontent.com", "data-framer-")),
        ("Framework", "Next.js", ("__next_data__", "/_next/", "next-router")),
        ("Framework", "Nuxt", ("__nuxt__", "__nuxt", "/_nuxt/")),
        ("Framework", "Astro", ("astro-island", "/_astro/")),
        ("Framework", "Gatsby", ("___gatsby", "gatsby-chunk-mapping", "gatsby-image")),
        ("Framework", "Svelte", ("sveltekit", "svelte-hmr", "data-svelte-h")),
        ("Framework", "Vue.js", ("__vue__", "data-v-", "vue.runtime")),
        ("Framework", "Angular", ("ng-version=", "angular/core", "ng-app=")),
        ("Framework", "React", ("react-dom", "data-reactroot", "__react")),
        ("Framework", "Remix", ("__remixcontext", "remix.run", "remixmanifest")),
        ("Framework", "Vite", ("/@vite/client", "/vite.svg")),
        ("Library", "jQuery", ("jquery.min.js", "jquery.js", "jquery-migrate")),
        ("Library", "Bootstrap", ("bootstrap.min.css", "bootstrap.min.js", "bootstrap.bundle")),
        ("Library", "Alpine.js", ("alpinejs", "x-data=", "alpine:init")),
        ("Library", "HTMX", ("htmx.org", "hx-get=", "hx-post=")),
    ]
    for category, name, markers in checks:
        found = [marker for marker in markers if marker in evidence_text]
        if found:
            confidence = "high" if category == "CMS" or name in {"Next.js", "Nuxt", "Astro", "Gatsby", "Angular"} else "medium"
            add(category, name, confidence, "Found marker(s): " + ", ".join(found[:4]))

    server = headers.get("server", "")
    powered_by = headers.get("x-powered-by", "")
    via = headers.get("via", "")
    if server:
        add("HTTP server header", server, "high", f"Server response header: {server}")
    if powered_by:
        add("Server-side hint", powered_by, "high", f"X-Powered-By response header: {powered_by}")

    language_rules = [
        ("PHP", ("x-powered-by: php", "phpsessid", ".php")),
        ("ASP.NET", ("x-powered-by: asp.net", "asp.net_sessionid", ".aspx", "x-aspnet-version")),
        ("Java", ("jsessionid", ".jsp", "apache tomcat")),
        ("Python", ("server: gunicorn", "server: uwsgi", "x-powered-by: django", "django_session")),
        ("Node.js", ("x-powered-by: express", "server: node.js")),
        ("Ruby", ("phusion passenger", "rack.version", "x-powered-by: ruby")),
    ]
    for name, markers in language_rules:
        found = [marker for marker in markers if marker in evidence_text]
        if found:
            add("Likely backend language", name, "high" if any(marker in found for marker in markers[:3]) else "medium", "Evidence: " + ", ".join(found))
    if page.get("script_count", 0):
        add("Client-side language", "JavaScript", "high", f"HTML contains {page['script_count']} script tag(s).")

    platform_markers = [
        ("Cloudflare", ("cf-ray", "server: cloudflare", "cloudflare.com")),
        ("Vercel", ("x-vercel-id", "server: vercel", "vercel-dns.com")),
        ("Netlify", ("x-nf-request-id", "server: netlify", "netlify.app")),
        ("Amazon CloudFront", ("x-amz-cf-id", "cloudfront.net", "server: cloudfront")),
        ("Amazon S3", ("s3.amazonaws.com", "x-amz-bucket-region")),
        ("Fastly", ("fastly.net", "via: 1.1 varnish", "x-served-by: cache-")),
        ("Akamai", ("akamai.net", "akamaihd.net", "x-akamai-transformed")),
        ("Microsoft Azure", ("azurewebsites.net", "azurefd.net", "x-azure-ref")),
        ("Google Firebase Hosting", ("web.app", "firebaseapp.com", "x-firebase")),
    ]
    for name, markers in platform_markers:
        found = [marker for marker in markers if marker in evidence_text or marker in (via + server).lower()]
        if found:
            confidence = "medium" if name == "Fastly" and "fastly.net" not in found else "high"
            add("Hosting / edge", name, confidence, "Found marker(s): " + ", ".join(found[:4]))

    analytics_markers = [
        ("Google Analytics 4", re.compile(r"\bG-[A-Z0-9]{5,}\b", re.I)),
        ("Google Tag Manager", re.compile(r"\bGTM-[A-Z0-9]+\b", re.I)),
        ("Legacy Universal Analytics", re.compile(r"\bUA-\d+-\d+\b", re.I)),
    ]
    for name, pattern in analytics_markers:
        match = pattern.search(html_text)
        if match:
            add("Analytics", name, "high", f"Found ID {match.group(0)} in page HTML.")
    analytics_services = [
        ("Meta Pixel", ("connect.facebook.net", "fbq(")),
        ("Microsoft Clarity", ("clarity.ms", "clarity(")),
        ("Hotjar", ("static.hotjar.com", "hotjar.com")),
        ("Plausible", ("plausible.io/js/",)),
        ("Matomo", ("matomo.js", "piwik.js")),
    ]
    for name, markers in analytics_services:
        found = [marker for marker in markers if marker in evidence_text]
        if found:
            add("Analytics", name, "medium", "Found marker(s): " + ", ".join(found))

    # Site builders ship their own React/Vue bundles; listing them as the site's framework is a false positive.
    builders = {name for (category, name) in detections if category == "CMS" and name in {"Wix", "Squarespace", "Webflow", "Framer", "Shopify"}}
    if builders:
        for generic in ("React", "Vue.js", "Svelte", "Vite"):
            detections.pop(("Framework", generic), None)

    consent = page.get("consent_tool")
    if consent:
        add("Consent", consent, "high", "Consent management markers found in page HTML.")

    return sorted(detections.values(), key=lambda item: (item["category"], item["name"]))
