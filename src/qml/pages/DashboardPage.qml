import QtQuick
import QtQuick.Layouts
import Hyperlancer

// Start page: greeting, a quick way to analyze one website, headline numbers, and the two
// lists worth checking every morning (latest analyses, things about to expire).
PageBase {
    id: page

    required property AppContext app
    property real ambientTime: 0

    readonly property var stats: app.dashboard.stats
    readonly property var recent: stats.recent || []
    readonly property var comingUp: stats.comingUp || []
    readonly property var expiringSites: stats.expiringSites || []
    readonly property var usage: stats.apiUsage || {}

    signal siteOpened(var siteId)
    signal sectionRequested(int index)

    readonly property string greeting: {
        const hour = new Date().getHours();
        if (hour < 5 || hour >= 22)
            return qsTr("Good night");
        if (hour < 12)
            return qsTr("Good morning");
        if (hour < 18)
            return qsTr("Good afternoon");
        return qsTr("Good evening");
    }

    function submit() {
        if (urlField.text.trim().length === 0)
            return;
        app.addSite(urlField.text, true);
        orb.playPulse();
    }

    onEntered: {
        app.dashboard.refresh();
        sitesTile.replay();
        analyzedTile.replay();
        failedTile.replay();
        expiringTile.replay();
        todosTile.replay();
    }

    Connections {
        target: page.app

        function onSiteAdded(siteId, created) {
            if (!page.isCurrent)
                return;
            urlField.clear();
            page.siteOpened(siteId);
        }
    }

    PageHeader {
        id: header

        width: parent.width
        title: page.greeting
        subtitle: page.app.jobRunner.runningCount > 0
                  ? qsTr("%n analysis(es) running in the background.", "", page.app.jobRunner.runningCount)
                  : qsTr("%n website(s) tracked. Paste a website to see what it needs.", "", page.stats.sites || 0)
        opacity: page.reveal(0)
        transform: Translate { y: page.revealShift(0) }
    }

    // Hero: the one action that starts every lead.
    GlassPanel {
        id: hero

        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingXl
        anchors.left: parent.left
        anchors.right: parent.right
        height: 132
        radius: Theme.radiusCard
        clip: true
        opacity: page.reveal(1)
        transform: Translate { y: page.revealShift(1) }

        EnergyOrb {
            id: orb

            width: 220
            height: 220
            anchors.right: parent.right
            anchors.rightMargin: -30
            anchors.verticalCenter: parent.verticalCenter
            time: page.ambientTime
            opacity: 0.85
        }

        Column {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingXl
            anchors.right: orb.left
            anchors.rightMargin: Theme.spacingL
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingM

            Text {
                text: qsTr("Analyze a website")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHeading + 3
                font.weight: Font.DemiBold
            }

            Row {
                width: parent.width
                spacing: Theme.spacingS

                StyledTextField {
                    id: urlField

                    width: Math.min(520, parent.width - analyzeButton.width - parent.spacing)
                    glyph: Icons.globe
                    placeholderText: qsTr("example.com.tr or https://…")
                    onAccepted: page.submit()
                }

                PrimaryButton {
                    id: analyzeButton

                    implicitHeight: Theme.inputHeight
                    implicitWidth: 132
                    text: qsTr("Analyze")
                    glyph: Icons.lightning
                    enabled: urlField.text.trim().length > 0 && page.app.database.state === Database.Ready
                    breath: page.isCurrent ? 0.5 + 0.5 * Math.sin(page.ambientTime * 1.7) : 0
                    onClicked: page.submit()
                }
            }
        }
    }

    RowLayout {
        id: tiles

        anchors.top: hero.bottom
        anchors.topMargin: Theme.spacingL
        anchors.left: parent.left
        anchors.right: parent.right
        height: 112
        spacing: Theme.spacingL

        StatTile {
            id: sitesTile

            Layout.fillWidth: true
            Layout.fillHeight: true
            glyph: Icons.globe
            label: qsTr("Websites")
            value: page.stats.sites || 0
            note: qsTr("view all")
            opacity: page.reveal(2)
            transform: Translate { y: page.revealShift(2) }
            onClicked: page.sectionRequested(1)
        }

        StatTile {
            id: analyzedTile

            Layout.fillWidth: true
            Layout.fillHeight: true
            glyph: Icons.sealCheck
            label: qsTr("Analyzed")
            value: page.stats.analyzed || 0
            note: (page.stats.sites || 0) > 0 ? Math.round(100 * (page.stats.analyzed || 0) / page.stats.sites) + "%" : ""
            opacity: page.reveal(3)
            transform: Translate { y: page.revealShift(3) }
        }

        StatTile {
            id: failedTile

            Layout.fillWidth: true
            Layout.fillHeight: true
            glyph: Icons.warningCircle
            label: qsTr("Unreachable")
            value: page.stats.failed || 0
            note: (page.stats.failed || 0) > 0 ? qsTr("needs a look") : qsTr("none")
            noteColor: (page.stats.failed || 0) > 0 ? Theme.warning : Theme.accent
            opacity: page.reveal(4)
            transform: Translate { y: page.revealShift(4) }
        }

        StatTile {
            id: expiringTile

            Layout.fillWidth: true
            Layout.fillHeight: true
            glyph: Icons.calendar
            label: qsTr("Expiring")
            value: page.stats.expiring || 0
            note: qsTr("≤ 30 days")
            noteColor: (page.stats.expiring || 0) > 0 ? Theme.warning : Theme.accent
            opacity: page.reveal(5)
            transform: Translate { y: page.revealShift(5) }
        }

        StatTile {
            id: todosTile

            Layout.fillWidth: true
            Layout.fillHeight: true
            glyph: Icons.checkSquare
            label: qsTr("Open to-dos")
            value: page.stats.openTodos || 0
            note: (page.stats.overdueTodos || 0) > 0 ? qsTr("%n overdue", "", page.stats.overdueTodos) : qsTr("none overdue")
            noteColor: (page.stats.overdueTodos || 0) > 0 ? Theme.danger : Theme.accent
            opacity: page.reveal(5)
            transform: Translate { y: page.revealShift(5) }
        }
    }

    RowLayout {
        anchors.top: tiles.bottom
        anchors.topMargin: Theme.spacingL
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: Theme.spacingL

        // Latest analyses.
        SectionPanel {
            id: recentPanel

            // Rows that fit under the header; the list never runs past the panel.
            readonly property int capacity: Math.max(0, Math.floor((height - 64) / Theme.compactRowHeight))

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 2
            clip: true
            title: qsTr("Recently analyzed")
            glyph: Icons.history
            opacity: page.reveal(6)
            transform: Translate { y: page.revealShift(6) }

            Repeater {
                model: page.recent.slice(0, recentPanel.capacity)

                delegate: SiteMiniRow {
                    required property var modelData
                    required property int index

                    width: parent.width
                    siteKey: modelData.site_key
                    faviconUrl: modelData.favicon_url || ""
                    detail: [modelData.cms, modelData.title].filter(v => !!v).join(" · ")
                    trailing: Format.relativeTime(modelData.last_fetched_at)
                    status: modelData.last_fetch_outcome || ""
                    last: index === Math.min(page.recent.length, recentPanel.capacity) - 1
                    onClicked: page.siteOpened(modelData.id)
                }
            }

            EmptyState {
                width: parent.width
                visible: page.recent.length === 0
                glyph: Icons.lightning
                title: qsTr("No analyses yet")
                text: qsTr("Paste a website above, or find leads in Discover.")
            }
        }

        // To-dos and deadlines that are overdue or due within 30 days.
        SectionPanel {
            id: comingUpPanel

            readonly property int capacity: Math.max(0, Math.floor((height - 64) / Theme.compactRowHeight))

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 2
            clip: true
            title: qsTr("Coming up")
            glyph: Icons.checkSquare
            opacity: page.reveal(7)
            transform: Translate { y: page.revealShift(7) }

            Repeater {
                model: page.comingUp.slice(0, comingUpPanel.capacity)

                delegate: SiteMiniRow {
                    required property var modelData
                    required property int index

                    readonly property var days: Format.daysUntil(modelData.dueOn)

                    width: parent.width
                    siteKey: modelData.siteKey
                    detail: modelData.kind === "deadline" ? qsTr("Deadline") : modelData.title
                    trailing: Format.dueLabel(days)
                    trailingColor: Format.dueColor(days)
                    last: index === Math.min(page.comingUp.length, comingUpPanel.capacity) - 1
                    onClicked: page.siteOpened(modelData.siteId)
                }
            }

            Text {
                width: parent.width
                topPadding: Theme.spacingS
                visible: page.comingUp.length === 0
                text: qsTr("No deadlines or to-dos due in the next 30 days.")
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1.6
            spacing: Theme.spacingL

            SectionPanel {
                id: expiringPanel

                readonly property int capacity: Math.max(0, Math.floor((height - 64) / Theme.compactRowHeight))

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                title: qsTr("Expiring soon")
                glyph: Icons.calendar
                opacity: page.reveal(7)
                transform: Translate { y: page.revealShift(7) }

                Repeater {
                    model: page.expiringSites.slice(0, expiringPanel.capacity)

                    delegate: SiteMiniRow {
                        required property var modelData
                        required property int index

                        readonly property var sslDays: Format.daysUntil(modelData.tls_valid_until)
                        readonly property var domainDays: Format.daysUntil(modelData.domain_expires_on)
                        readonly property bool sslFirst: sslDays !== null && (domainDays === null || sslDays <= domainDays)

                        width: parent.width
                        siteKey: modelData.site_key
                        detail: sslFirst ? qsTr("SSL certificate") : qsTr("Domain registration")
                        trailing: Format.daysLabel(sslFirst ? sslDays : domainDays)
                        trailingColor: Format.daysColor(sslFirst ? sslDays : domainDays)
                        last: index === Math.min(page.expiringSites.length, expiringPanel.capacity) - 1
                        onClicked: page.siteOpened(modelData.id)
                    }
                }

                Text {
                    width: parent.width
                    topPadding: Theme.spacingS
                    visible: page.expiringSites.length === 0
                    text: qsTr("Nothing expires in the next 30 days.")
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }
            }

            SectionPanel {
                Layout.fillWidth: true
                title: qsTr("API usage this month")
                glyph: Icons.chartBar
                opacity: page.reveal(8)
                transform: Translate { y: page.revealShift(8) }

                FactRow {
                    labelWidth: width * 0.7
                    label: qsTr("LangSearch searches")
                    value: String(page.usage.langsearch || 0)
                    copyable: false
                }

                FactRow {
                    labelWidth: width * 0.7
                    label: qsTr("PageSpeed checks")
                    value: String(page.usage.pagespeed || 0)
                    copyable: false
                    last: true
                }
            }
        }
    }

    // One compact line: initial badge, domain, a muted detail, and a value on the right.
    component SiteMiniRow: Item {
        id: miniRow

        property string siteKey
        property string faviconUrl
        property string detail
        property string trailing
        property color trailingColor: Theme.textMuted
        property string status: ""
        property bool last: false

        signal clicked

        implicitHeight: Theme.compactRowHeight

        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: -8
            anchors.rightMargin: -8
            radius: Theme.radiusSmall
            color: rowHover.hovered ? Theme.rowHover : "transparent"

            Behavior on color { ColorAnimation { duration: Motion.fast } }
        }

        SiteBadge {
            id: badge

            anchors.verticalCenter: parent.verticalCenter
            siteKey: miniRow.siteKey
            faviconUrl: miniRow.faviconUrl
            status: miniRow.status
            size: 28
        }

        Column {
            anchors.left: badge.right
            anchors.leftMargin: Theme.spacingM
            anchors.right: trailingText.left
            anchors.rightMargin: Theme.spacingM
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1

            Text {
                width: parent.width
                text: miniRow.siteKey
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.weight: Font.Medium
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: miniRow.detail
                visible: text.length > 0
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
        }

        Text {
            id: trailingText

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: miniRow.trailing
            color: miniRow.trailingColor
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption + 1
            font.weight: Font.Medium
        }

        Rectangle {
            anchors.left: badge.right
            anchors.leftMargin: Theme.spacingM
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.rowDivider
            visible: !miniRow.last
        }

        HoverHandler {
            id: rowHover

            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            onTapped: miniRow.clicked()
        }
    }
}
