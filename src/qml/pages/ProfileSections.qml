import QtQuick
import Hyperlancer

// The full profile of one snapshot, as titled glass sections in two columns, in the order of
// the PRD's "existing checks" table. A section whose check failed shows a badge and the
// error; the others still render. Click any value to copy it.
Flickable {
    id: sections

    property var summary: ({})
    property var speed: ({})

    signal copyRequested(string text)
    signal linkRequested(string url)

    readonly property var http: summary.http || ({})
    readonly property var urls: summary.url || ({})
    readonly property var hosting: summary.hosting || ({})
    readonly property var dns: summary.dns || ({})
    readonly property var tls: summary.tls || ({})
    readonly property var domain: summary.domain || ({})
    readonly property var seo: summary.seo || ({})
    readonly property var sitemaps: summary.sitemaps || ({})
    readonly property var contact: summary.contact || ({})
    readonly property var security: summary.security || ({})
    readonly property var archive: summary.archive || ({})
    readonly property var design: summary.design || ({})
    readonly property var cms: summary.cms || ({})
    readonly property var tech: summary.tech || []
    readonly property var errors: summary.errors || []
    readonly property real columnWidth: (width - Theme.spacingL) / 2

    function errorFor(section) {
        for (let i = 0; i < errors.length; ++i) {
            if (errors[i].section === section)
                return errors[i].message;
        }
        return "";
    }

    function list(values, separator) {
        if (!values || values.length === 0)
            return "—";
        return values.join(separator === undefined ? "\n" : separator);
    }

    function check(value, goodWhenTrue) {
        if (value === null || value === undefined)
            return "—";
        return value ? (goodWhenTrue === false ? qsTr("Yes") : qsTr("Yes")) : qsTr("No");
    }

    function lengthNote(text, length, low, high) {
        if (!text)
            return qsTr("Missing");
        const note = length < low ? qsTr("short") : length > high ? qsTr("long") : qsTr("good");
        return text + "  (" + length + ", " + note + ")";
    }

    contentHeight: Math.max(leftColumn.height, rightColumn.height) + Theme.spacingL
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    Column {
        id: leftColumn

        width: sections.columnWidth
        spacing: Theme.spacingL

        GlassPanel {
            width: parent.width
            height: problemsColumn.implicitHeight + 2 * Theme.spacingL
            radius: Theme.radiusCard
            visible: sections.errors.length > 0
            border.color: Theme.withAlpha(Theme.warning, 0.45)

            Column {
                id: problemsColumn

                x: Theme.spacingL + 2
                y: Theme.spacingL
                width: parent.width - 2 * (Theme.spacingL + 2)
                spacing: Theme.spacingS

                Row {
                    spacing: Theme.spacingS

                    Icon {
                        glyph: Icons.warning
                        color: Theme.warning
                        size: 16
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: qsTr("%n check(s) did not complete", "", sections.errors.length)
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontHeading
                        font.weight: Font.DemiBold
                    }
                }

                Repeater {
                    model: sections.errors

                    delegate: Text {
                        required property var modelData

                        width: problemsColumn.width
                        text: "<b>" + modelData.section + "</b>  " + modelData.message
                        textFormat: Text.StyledText
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.WrapAnywhere
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Section {
            title: qsTr("Overview")
            glyph: Icons.browser
            failure: sections.errorFor("http")

            Fact { label: qsTr("Final URL"); value: Format.orDash(sections.urls.final) }
            Fact { label: qsTr("HTTP status"); value: Format.orDash(sections.http.status) }
            Fact {
                label: qsTr("Redirects")
                value: (sections.urls.redirects || []).length === 0 ? qsTr("None")
                       : sections.urls.redirects.map(r => r.status + " → " + r.to).join("\n")
            }
            Fact {
                label: qsTr("HTTP → HTTPS")
                value: Format.yesNo(sections.http.httpsRedirect)
                valueColor: sections.http.httpsRedirect === false ? Theme.warning : Theme.textPrimary
            }
            Fact { label: qsTr("www handling"); value: Format.orDash(sections.http.wwwCanonical) }
            Fact {
                label: qsTr("Bot protection")
                value: sections.http.blocked ? qsTr("Blocked by %1").arg(sections.http.blockedBy || qsTr("a challenge page")) : qsTr("None seen")
                valueColor: sections.http.blocked ? Theme.warning : Theme.textPrimary
                last: true
            }
        }

        Section {
            title: qsTr("Hosting")
            glyph: Icons.hardDrives
            failure: sections.errorFor("dns")

            Fact { label: qsTr("Provider"); value: Format.orDash(sections.hosting.edge || sections.hosting.org) }
            Fact { label: qsTr("Network (ASN)"); value: [sections.hosting.asn ? "AS" + sections.hosting.asn : "", sections.hosting.org || ""].filter(v => v).join("  ") || "—" }
            Fact { label: qsTr("IPv4"); value: sections.list(sections.hosting.ips, ", ") }
            Fact { label: qsTr("IPv6"); value: sections.list(sections.hosting.ipv6, ", ") }
            Fact { label: qsTr("Reverse DNS"); value: Format.orDash(sections.hosting.reverseDns) }
            Fact { label: qsTr("Country"); value: Format.orDash(sections.hosting.country) }
            Fact { label: qsTr("Web server"); value: Format.orDash(sections.hosting.server) }
            Fact {
                label: qsTr("Backend")
                value: sections.hosting.language ? sections.hosting.language + (sections.hosting.languageVersion ? " " + sections.hosting.languageVersion : "") : "—"
                last: true
            }
        }

        Section {
            title: qsTr("DNS and email")
            glyph: Icons.envelope
            failure: sections.errorFor("dns")

            Fact { label: qsTr("Nameservers"); value: sections.list(sections.dns.ns) }
            Fact { label: qsTr("Mail servers"); value: (sections.dns.mx || []).length === 0 ? qsTr("None") : sections.dns.mx.map(m => m.pref + "  " + m.host).join("\n") }
            Fact { label: qsTr("Email provider"); value: Format.orDash(sections.dns.emailProvider) }
            Fact {
                label: qsTr("SPF")
                value: sections.dns.spf || qsTr("Missing")
                valueColor: sections.dns.spf ? Theme.textPrimary : Theme.warning
                mono: !!sections.dns.spf
            }
            Fact {
                label: qsTr("DMARC")
                value: sections.dns.dmarc || qsTr("Missing")
                valueColor: !sections.dns.dmarc || String(sections.dns.dmarc).indexOf("p=none") >= 0 ? Theme.warning : Theme.textPrimary
                mono: !!sections.dns.dmarc
            }
            Fact { label: qsTr("DNSSEC"); value: Format.yesNo(sections.dns.dnssec) }
            Fact { label: qsTr("CAA"); value: sections.list(sections.dns.caa); last: true }
        }

        Section {
            title: qsTr("SSL / TLS")
            glyph: Icons.lockSimple
            failure: sections.errorFor("tls")

            Fact {
                label: qsTr("Trusted")
                value: sections.tls.verified ? qsTr("Yes") : qsTr("No — browsers show a warning")
                valueColor: sections.tls.verified ? Theme.success : Theme.danger
            }
            Fact { label: qsTr("Issuer"); value: Format.orDash(sections.tls.issuer) }
            Fact { label: qsTr("Valid from"); value: Format.shortDate(sections.tls.validFrom) || "—" }
            Fact {
                label: qsTr("Valid until")
                value: sections.tls.validUntil ? Format.shortDate(sections.tls.validUntil) + "  (" + Format.daysLabel(Math.floor(sections.tls.daysLeft)) + ")" : "—"
                valueColor: Format.daysColor(sections.tls.daysLeft === null || sections.tls.daysLeft === undefined ? null : Math.floor(sections.tls.daysLeft))
            }
            Fact { label: qsTr("Names covered"); value: (sections.tls.sanCount || 0) + ((sections.tls.sans || []).length ? "  ·  " + sections.tls.sans.slice(0, 4).join(", ") : "") }
            Fact { label: qsTr("Fingerprint"); value: Format.orDash(sections.tls.fingerprint); mono: true; last: true }
        }

        Section {
            title: qsTr("Domain registration")
            glyph: Icons.identification
            failure: sections.errorFor("registration")

            Fact { label: qsTr("Registrar"); value: Format.orDash(sections.domain.registrar) }
            Fact { label: qsTr("Registrant"); value: Format.orDash(sections.domain.registrant) }
            Fact { label: qsTr("Created"); value: sections.domain.created ? Format.shortDate(sections.domain.created) + (sections.domain.ageYears ? "  (" + qsTr("%1 years").arg(sections.domain.ageYears.toFixed(1)) + ")" : "") : "—" }
            Fact { label: qsTr("Expires"); value: Format.shortDate(sections.domain.expires) || "—"; valueColor: Format.daysColor(Format.daysUntil(sections.domain.expires)) }
            Fact { label: qsTr("Transfer lock"); value: Format.yesNo(sections.domain.transferLocked) }
            Fact { label: qsTr("Statuses"); value: sections.list(sections.domain.statuses); last: true }
        }

        Section {
            title: qsTr("Web archive")
            glyph: Icons.history
            failure: sections.errorFor("archive")

            Fact { label: qsTr("First capture"); value: Format.shortDate(sections.archive.first) || "—" }
            Fact { label: qsTr("Latest capture"); value: Format.shortDate(sections.archive.last) || "—"; last: true }
        }
    }

    Column {
        id: rightColumn

        x: sections.columnWidth + Theme.spacingL
        width: sections.columnWidth
        spacing: Theme.spacingL

        Section {
            title: qsTr("Technology")
            glyph: Icons.stack
            failure: sections.errorFor("page")

            Fact {
                label: qsTr("CMS")
                value: sections.cms.name ? sections.cms.name + (sections.cms.version ? " " + sections.cms.version : "") : "—"
            }
            Fact { label: qsTr("Theme"); value: Format.orDash(sections.cms.theme); visible: !!sections.cms.theme }
            Fact { label: qsTr("Plugins"); value: sections.list(sections.cms.plugins, ", "); visible: (sections.cms.plugins || []).length > 0 }

            Repeater {
                model: sections.tech

                delegate: Fact {
                    required property var modelData
                    required property int index

                    label: modelData.category
                    value: modelData.name + (modelData.version ? " " + modelData.version : "")
                           + (modelData.confidence !== "high" ? "  · " + modelData.confidence : "")
                    last: index === sections.tech.length - 1
                }
            }
        }

        Section {
            title: qsTr("Design age")
            glyph: Icons.paintBrush
            visible: sections.design.estimatedFrom || (sections.design.clues || []).length > 0

            Fact {
                label: qsTr("Estimated from")
                value: sections.design.estimatedFrom ? String(sections.design.estimatedFrom) + (sections.design.confidence ? "  (" + sections.design.confidence + " confidence)" : "") : "—"
            }
            Fact { label: qsTr("Newest asset"); value: Format.orDash(sections.design.latestAsset) }

            Repeater {
                model: (sections.design.clues || []).slice(0, 6)

                delegate: Fact {
                    required property var modelData
                    required property int index

                    label: modelData.label
                    value: modelData.date + (modelData.detail ? "  · " + modelData.detail : "")
                    last: index === Math.min(5, sections.design.clues.length - 1)
                }
            }
        }

        Section {
            title: qsTr("SEO and meta")
            glyph: Icons.article
            failure: sections.errorFor("page")

            Fact { label: qsTr("Title"); value: sections.lengthNote(sections.seo.title, sections.seo.titleLength || 0, 30, 65); valueColor: sections.seo.title ? Theme.textPrimary : Theme.warning }
            Fact { label: qsTr("Description"); value: sections.lengthNote(sections.seo.description, sections.seo.descriptionLength || 0, 70, 160); valueColor: sections.seo.description ? Theme.textPrimary : Theme.warning }
            Fact {
                label: qsTr("Mobile viewport")
                value: sections.seo.viewport || qsTr("Missing — not mobile-friendly")
                valueColor: sections.seo.viewport ? Theme.textPrimary : Theme.danger
            }
            Fact { label: qsTr("Language"); value: Format.orDash(sections.seo.lang) }
            Fact { label: qsTr("hreflang"); value: (sections.seo.hreflang || []).length ? sections.seo.hreflang.map(h => h.lang || h.hreflang || h).join(", ") : qsTr("None") }
            Fact { label: qsTr("Canonical"); value: Format.orDash(sections.seo.canonical) }
            Fact { label: qsTr("Robots"); value: sections.seo.noindex ? qsTr("noindex — hidden from search") : Format.orDash(sections.seo.robots); valueColor: sections.seo.noindex ? Theme.danger : Theme.textPrimary }
            Fact { label: qsTr("Open Graph"); value: Format.orDash(sections.seo.ogTitle) }
            Fact { label: qsTr("Structured data"); value: sections.list(sections.seo.structuredData, ", ") }
            Fact { label: qsTr("H1 headings"); value: String(sections.seo.h1Count || 0); valueColor: (sections.seo.h1Count || 0) === 1 ? Theme.textPrimary : Theme.warning }
            Fact {
                label: qsTr("Images without alt")
                value: (sections.seo.imagesMissingAlt || 0) + " / " + (sections.seo.imagesTotal || 0)
                last: true
            }
        }

        Section {
            title: qsTr("Performance")
            glyph: Icons.gauge
            failure: sections.errorFor("http")

            Fact { label: qsTr("First byte"); value: Format.ms(sections.http.ttfbMs) }
            Fact { label: qsTr("Total load"); value: Format.ms(sections.http.totalMs) }
            Fact { label: qsTr("HTML size"); value: Format.bytes(sections.http.htmlBytes) }
            Fact { label: qsTr("Compression"); value: Format.orDash(sections.http.compression) }
            Fact { label: qsTr("HTTP version"); value: Format.orDash(sections.http.httpVersion) }
            Fact {
                label: qsTr("Mobile score")
                value: sections.speed.score !== undefined && sections.speed.score !== null ? sections.speed.score + " / 100" : qsTr("Not measured — use Mobile speed")
                valueColor: sections.speed.score === undefined || sections.speed.score === null ? Theme.textMuted
                            : sections.speed.score < 50 ? Theme.danger : sections.speed.score < 90 ? Theme.warning : Theme.success
            }

            Repeater {
                model: sections.speed.metrics ? Object.keys(sections.speed.metrics) : []

                delegate: Fact {
                    required property string modelData

                    readonly property var metric: sections.speed.metrics[modelData]
                    readonly property var names: ({
                        firstContentfulPaint: qsTr("First contentful paint"),
                        largestContentfulPaint: qsTr("Largest contentful paint"),
                        totalBlockingTime: qsTr("Total blocking time"),
                        cumulativeLayoutShift: qsTr("Layout shift"),
                        speedIndex: qsTr("Speed index")
                    })

                    label: names[modelData] || modelData
                    value: metric.display || "—"
                    valueColor: metric.score >= 0.9 ? Theme.success : metric.score >= 0.5 ? Theme.warning : metric.score >= 0 ? Theme.danger : Theme.textPrimary
                }
            }
        }

        Section {
            title: qsTr("Contact channels")
            glyph: Icons.at
            failure: sections.errorFor("page")

            Fact { label: qsTr("Emails"); value: sections.list(sections.contact.emails) }
            Fact { label: qsTr("Phones"); value: sections.list(sections.contact.phones) }
            Fact {
                label: qsTr("WhatsApp")
                value: (sections.contact.whatsapp || []).length ? sections.contact.whatsapp.map(w => typeof w === "string" ? w : (w.number || w.url || JSON.stringify(w))).join("\n") : qsTr("None found")
                valueColor: (sections.contact.whatsapp || []).length ? Theme.success : Theme.textMuted
            }
            Fact { label: qsTr("Address"); value: Format.orDash(sections.contact.address) }

            Repeater {
                model: sections.contact.socials || []

                delegate: Fact {
                    required property var modelData

                    readonly property string link: typeof modelData === "string" ? modelData : (modelData.url || "")
                    label: typeof modelData === "string" ? qsTr("Social") : (modelData.network || modelData.platform || qsTr("Social"))
                    value: link
                    valueColor: Theme.accent
                    copyable: false

                    TapHandler {
                        onTapped: sections.linkRequested(parent.link)
                    }
                }
            }
        }

        Section {
            title: qsTr("Security headers")
            glyph: Icons.shieldCheck

            Fact { label: qsTr("Present"); value: sections.list(sections.security.present); valueColor: Theme.success }
            Fact { label: qsTr("Missing"); value: sections.list(sections.security.missing); valueColor: (sections.security.missing || []).length ? Theme.warning : Theme.textPrimary }
            Fact {
                label: qsTr("Cookies")
                value: (sections.security.cookies || []).length === 0 ? qsTr("None")
                       : sections.security.cookies.map(c => c.name + (c.secure ? "" : "  ⚠ not Secure") + (c.httpOnly ? "" : "  ⚠ not HttpOnly")).join("\n")
                last: true
            }
        }

        Section {
            title: qsTr("Pages and sitemap")
            glyph: Icons.fileText
            failure: sections.errorFor("sitemaps")

            Fact { label: qsTr("robots.txt"); value: sections.sitemaps.robotsStatus ? qsTr("HTTP %1 · %2 disallow rules").arg(sections.sitemaps.robotsStatus).arg(sections.sitemaps.disallowRules || 0) : qsTr("Not found") }
            Fact { label: qsTr("Sitemap files"); value: (sections.sitemaps.files || []).length ? sections.sitemaps.files.map(f => f.url + (f.error ? "  (" + f.error + ")" : "")).join("\n") : qsTr("None") }
            Fact { label: qsTr("URLs listed"); value: String(sections.sitemaps.totalUrls || 0) + (sections.sitemaps.truncated ? qsTr(" (truncated)") : "") }
            Fact {
                label: qsTr("Sample")
                value: (sections.sitemaps.urls || []).length ? sections.sitemaps.urls.slice(0, 8).map(u => u.loc).join("\n") : "—"
                last: true
            }
        }
    }

    // A SectionPanel sized to the column, with the check's failure shown in its header.
    component Section: SectionPanel {
        property string failure: ""

        width: parent.width
        status: failure.length > 0 ? "failed" : ""
        statusText: failure.length > 0 ? qsTr("Check failed") : ""
    }

    component Fact: FactRow {
        onCopyRequested: text => sections.copyRequested(text)
    }
}
