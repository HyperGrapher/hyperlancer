import QtQuick
import QtQuick.Layouts
import Hyperlancer

// One site: header with actions, live analysis steps while the profiler runs, the key facts
// at a glance, then tabs for the full profile, emails, snapshot history and activity.
Item {
    id: view

    required property AppContext app
    property bool ambientActive: true
    property int currentTab: 0

    readonly property SiteDetail detail: app.siteDetail
    readonly property var site: detail.site
    readonly property var snapshot: detail.snapshot
    readonly property var summary: snapshot.summary || ({})
    readonly property var speed: detail.speed
    readonly property string siteKey: site.site_key || ""
    readonly property string outcome: site.last_fetch_outcome || ""
    readonly property var sslDays: Format.daysUntil(site.tls_valid_until)
    readonly property var domainDays: Format.daysUntil(site.domain_expires_on)
    readonly property bool hasProfile: summary.outcome !== undefined
    readonly property var steps: [qsTr("Resolve"), qsTr("Fetch"), qsTr("Read page"), qsTr("DNS & host"),
                                  qsTr("SSL"), qsTr("Registry"), qsTr("Sitemaps"), qsTr("Archive")]

    signal backRequested

    onSiteKeyChanged: currentTab = 0

    // ---- header ----
    Item {
        id: header

        width: parent.width
        height: 64

        IconButton {
            id: backButton

            anchors.verticalCenter: parent.verticalCenter
            glyph: Icons.arrowLeft
            hint: qsTr("Back to websites (Esc)")
            onClicked: view.backRequested()
        }

        SiteBadge {
            id: badge

            anchors.left: backButton.right
            anchors.leftMargin: Theme.spacingL
            anchors.verticalCenter: parent.verticalCenter
            siteKey: view.siteKey
            faviconUrl: view.site.favicon_url || ""
            status: view.detail.analyzing ? "running" : view.outcome
            size: 48
        }

        Column {
            anchors.left: badge.right
            anchors.leftMargin: Theme.spacingL
            anchors.right: actions.left
            anchors.rightMargin: Theme.spacingL
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4

            Row {
                spacing: Theme.spacingM
                width: parent.width

                Text {
                    id: titleText

                    text: view.siteKey
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTitle + 2
                    font.weight: Font.Bold
                    font.letterSpacing: -0.4
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, parent.width - outcomeBadge.width - Theme.spacingM)
                }

                Badge {
                    id: outcomeBadge

                    anchors.verticalCenter: titleText.verticalCenter
                    visible: text.length > 0
                    text: view.detail.analyzing ? qsTr("Analyzing")
                          : view.outcome === "ok" ? qsTr("Complete")
                          : view.outcome === "partial" ? qsTr("Partial")
                          : view.outcome === "failed" ? qsTr("Failed") : qsTr("Not analyzed")
                    tint: view.detail.analyzing ? Theme.accent : Theme.statusColor(view.outcome)
                }
            }

            Text {
                width: parent.width
                text: [view.site.title || "",
                       view.site.last_fetched_at ? qsTr("analyzed %1").arg(Format.relativeTime(view.site.last_fetched_at)) : ""]
                      .filter(v => v.length > 0).join("  ·  ")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                elide: Text.ElideRight
            }
        }

        Row {
            id: actions

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingS

            IconButton {
                implicitWidth: 40
                implicitHeight: 40
                glyph: Icons.trash
                danger: true
                hint: qsTr("Remove site (double-click)")
                onDoubleClicked: view.app.removeSite(view.detail.siteId)
            }

            IconButton {
                implicitWidth: 40
                implicitHeight: 40
                glyph: Icons.openExternal
                hint: qsTr("Open website")
                onClicked: view.app.openExternal(view.site.final_url || view.site.url)
            }

            IconButton {
                implicitWidth: 40
                implicitHeight: 40
                glyph: Icons.deviceMobile
                hint: view.detail.speedRunning ? qsTr("Measuring…") : qsTr("Check mobile speed")
                enabled: !view.detail.speedRunning
                onClicked: view.detail.checkSpeed()
            }

            PrimaryButton {
                implicitWidth: 150
                implicitHeight: 40
                text: view.detail.analyzing ? qsTr("Analyzing…") : view.outcome ? qsTr("Analyze again") : qsTr("Analyze")
                glyph: Icons.lightning
                enabled: !view.detail.analyzing && view.site.url !== undefined
                onClicked: view.detail.analyze()
            }
        }
    }

    // ---- live progress ----
    GlassPanel {
        id: progressPanel

        anchors.top: header.bottom
        anchors.topMargin: view.detail.analyzing ? Theme.spacingL : 0
        width: parent.width
        height: view.detail.analyzing ? 96 : 0
        radius: Theme.radiusCard
        clip: true
        opacity: view.detail.analyzing ? 1 : 0

        Behavior on height { NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: Motion.normal } }
        Behavior on anchors.topMargin { NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic } }

        // Horizontal step track: the vertical StepTimeline does not fit a banner, so the
        // same states are drawn across.
        Row {
            anchors.centerIn: parent
            spacing: 0

            Repeater {
                model: view.steps

                delegate: Item {
                    id: step

                    required property int index
                    required property string modelData
                    readonly property bool done: index < view.detail.progressStep
                    readonly property bool active: index === view.detail.progressStep && view.detail.analyzing
                    readonly property real slot: Math.max(70, (progressPanel.width - 2 * Theme.spacingXl) / view.steps.length)

                    width: slot
                    height: 60

                    Rectangle {
                        x: step.slot / 2 + 12
                        y: 13
                        width: step.slot - 24
                        height: 2
                        radius: 1
                        visible: step.index < view.steps.length - 1
                        color: step.done ? Theme.withAlpha(Theme.accent, 0.8) : Qt.rgba(1, 1, 1, 0.1)

                        Behavior on color { ColorAnimation { duration: Motion.normal } }
                    }

                    Rectangle {
                        id: node

                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 4
                        width: 20
                        height: 20
                        radius: 10
                        color: step.done ? Theme.accent : step.active ? Theme.withAlpha(Theme.accent, 0.22) : Qt.rgba(1, 1, 1, 0.06)
                        border.width: step.done ? 0 : 1.5
                        border.color: step.active ? Theme.accent : Theme.glassStrokeStrong

                        Behavior on color { ColorAnimation { duration: Motion.normal } }

                        Icon {
                            anchors.centerIn: parent
                            glyph: Icons.check
                            size: 12
                            color: "#120A24"
                            visible: step.done
                        }

                        StatusDot {
                            anchors.centerIn: parent
                            size: 7
                            visible: step.active
                            color: Theme.accent
                            active: view.ambientActive
                        }
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: node.bottom
                        anchors.topMargin: 8
                        text: step.modelData
                        color: step.done || step.active ? Theme.textPrimary : Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption + 1
                        font.weight: step.active ? Font.DemiBold : Font.Normal
                    }
                }
            }
        }
    }

    // ---- facts strip ----
    RowLayout {
        id: facts

        anchors.top: progressPanel.bottom
        anchors.topMargin: Theme.spacingL
        width: parent.width
        height: 78
        spacing: Theme.spacingM

        FactTile {
            label: qsTr("HTTP")
            value: view.site.http_status ? String(view.site.http_status) : "—"
            detail: view.summary.http ? Format.ms(view.summary.http.ttfbMs) + qsTr(" first byte") : ""
            valueColor: (view.site.http_status || 0) >= 400 ? Theme.danger : Theme.textPrimary
        }

        FactTile {
            label: qsTr("CMS")
            value: Format.orDash(view.site.cms)
            detail: view.site.cms_version ? qsTr("version %1").arg(view.site.cms_version) : ""
        }

        FactTile {
            label: qsTr("Hosting")
            value: Format.orDash(view.site.hosting)
            detail: Format.orDash(view.site.ip_address)
        }

        FactTile {
            label: qsTr("SSL")
            value: view.sslDays === null ? "—" : view.sslDays < 0 ? qsTr("Expired") : qsTr("%1 days").arg(view.sslDays)
            valueColor: Format.daysColor(view.sslDays)
            detail: view.summary.tls ? Format.orDash(view.summary.tls.issuer) : ""
        }

        FactTile {
            label: qsTr("Domain")
            value: view.domainDays === null ? "—" : view.domainDays < 0 ? qsTr("Expired") : qsTr("%1 days").arg(view.domainDays)
            valueColor: Format.daysColor(view.domainDays)
            detail: Format.orDash(view.site.registrar)
        }

        GlassCard {
            Layout.preferredWidth: 168
            Layout.fillHeight: true
            interactive: false

            ProgressRing {
                id: scoreRing

                x: Theme.spacingM
                anchors.verticalCenter: parent.verticalCenter
                width: 56
                height: 56
                thickness: 6
                value: view.speed.score !== undefined && view.speed.score !== null ? view.speed.score / 100 : 0
                label: view.speed.score !== undefined && view.speed.score !== null ? String(view.speed.score) : "—"
            }

            Column {
                anchors.left: scoreRing.right
                anchors.leftMargin: Theme.spacingM
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingS
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                Text {
                    text: qsTr("Mobile score")
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption + 1
                }

                Text {
                    width: parent.width
                    readonly property bool failed: !!view.speed.error && (view.speed.score === undefined || view.speed.score === null)

                    text: view.detail.speedRunning ? qsTr("measuring…")
                          : failed ? qsTr("check failed")
                          : view.speed.fetchedAt ? Format.relativeTime(view.speed.fetchedAt) : qsTr("not measured")
                    color: failed && !view.detail.speedRunning ? Theme.warning : Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption + 1
                    elide: Text.ElideRight
                }
            }
        }
    }

    SegmentedControl {
        id: tabs

        anchors.top: facts.bottom
        anchors.topMargin: Theme.spacingL
        options: [qsTr("Profile"), qsTr("Workspace"), qsTr("Emails"), qsTr("History"), qsTr("Activity")]
        currentIndex: view.currentTab
        onActivated: index => {
            view.currentTab = index;
            if (index === 2)
                view.detail.loadEmails();
        }
    }

    Text {
        anchors.right: parent.right
        anchors.verticalCenter: tabs.verticalCenter
        visible: view.currentTab === 0 && view.snapshot.fetchedAt !== undefined
        text: qsTr("Snapshot of %1").arg(Format.dateTime(view.snapshot.fetchedAt))
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontCaption + 1
    }

    Item {
        id: tabArea

        anchors.top: tabs.bottom
        anchors.topMargin: Theme.spacingM
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        ProfileSections {
            anchors.fill: parent
            visible: view.currentTab === 0 && view.hasProfile
            summary: view.summary
            speed: view.speed
            onCopyRequested: text => {
                view.app.copyText(text);
                toastHint.show();
            }
            onLinkRequested: url => view.app.openExternal(url)
        }

        EmptyState {
            anchors.centerIn: parent
            width: parent.width
            visible: view.currentTab === 0 && !view.hasProfile && !view.detail.loading
            glyph: view.detail.analyzing ? Icons.hourglass : Icons.lightning
            title: view.detail.analyzing ? qsTr("Analysis in progress") : qsTr("Not analyzed yet")
            text: view.detail.analyzing ? qsTr("Sections appear here when the profiler finishes, usually within a minute.")
                                        : qsTr("Run an analysis to see hosting, technology, SEO, security and contact details.")
        }

        SiteWorkspace {
            anchors.fill: parent
            visible: view.currentTab === 1
            app: view.app
        }

        SiteEmails {
            anchors.fill: parent
            visible: view.currentTab === 2
            app: view.app
        }

        HistoryList {
            anchors.fill: parent
            visible: view.currentTab === 3
        }

        ActivityList {
            anchors.fill: parent
            visible: view.currentTab === 4
        }
    }

    Rectangle {
        id: toastHint

        function show() {
            opacity = 1;
            hideTimer.restart();
        }

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        width: hintText.implicitWidth + 28
        height: 30
        radius: 15
        color: Qt.rgba(0.04, 0.02, 0.08, 0.86)
        border.width: 1
        border.color: Theme.withAlpha(Theme.accent, 0.5)
        opacity: 0

        Behavior on opacity { NumberAnimation { duration: Motion.normal } }

        Text {
            id: hintText

            anchors.centerIn: parent
            text: qsTr("Copied")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption + 1
        }

        Timer {
            id: hideTimer

            interval: 1200
            onTriggered: toastHint.opacity = 0
        }
    }

    // A compact fact on glass: caption, value, one muted detail line.
    component FactTile: GlassCard {
        id: tile

        property string label
        property string value
        property string detail
        property color valueColor: Theme.textPrimary

        Layout.fillWidth: true
        Layout.fillHeight: true
        interactive: false

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.spacingM + 2
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3

            Text {
                text: tile.label
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                font.weight: Font.DemiBold
                font.letterSpacing: 1.0
                font.capitalization: Font.AllUppercase
            }

            Text {
                width: parent.width
                text: tile.value
                color: tile.valueColor
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHeading + 2
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: tile.detail.length > 0 ? tile.detail : " "
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
        }
    }

    component HistoryList: GlassPanel {
        radius: Theme.radiusCard
        clip: true

        TableHeader {
            id: historyHeader

            width: parent.width

            TableHeader.Caption { text: qsTr("Analyzed"); Layout.fillWidth: true }
            TableHeader.Caption { text: qsTr("Result"); Layout.preferredWidth: 96 }
            TableHeader.Caption { text: qsTr("HTTP"); Layout.preferredWidth: 56 }
            TableHeader.Caption { text: qsTr("Took"); Layout.preferredWidth: 70 }
            TableHeader.Caption { text: qsTr("Problem"); Layout.preferredWidth: 300 }
        }

        ListView {
            anchors.top: historyHeader.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: view.detail.snapshots

            delegate: Item {
                id: snapshotRow

                required property var modelData
                readonly property bool shown: view.snapshot.id === modelData.id

                width: ListView.view.width
                height: Theme.compactRowHeight

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSmall
                    color: snapshotRow.shown ? Theme.withAlpha(Theme.accent, 0.12) : snapshotHover.hovered ? Theme.rowHover : "transparent"
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingL - 6
                    anchors.rightMargin: Theme.spacingL - 6
                    spacing: Theme.spacingM

                    Text {
                        Layout.fillWidth: true
                        text: Format.dateTime(snapshotRow.modelData.fetched_at)
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                    }

                    Item {
                        Layout.preferredWidth: 96
                        Layout.preferredHeight: 22

                        Badge {
                            text: snapshotRow.modelData.outcome
                            tint: Theme.statusColor(snapshotRow.modelData.outcome)
                        }
                    }

                    Text {
                        Layout.preferredWidth: 56
                        text: Format.orDash(snapshotRow.modelData.http_status)
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                    }

                    Text {
                        Layout.preferredWidth: 70
                        text: Format.ms(snapshotRow.modelData.duration_ms)
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                    }

                    Text {
                        Layout.preferredWidth: 300
                        text: snapshotRow.modelData.error_summary || ""
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption + 1
                        elide: Text.ElideRight
                    }
                }

                HoverHandler {
                    id: snapshotHover

                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    onTapped: {
                        view.detail.showSnapshot(snapshotRow.modelData.id);
                        view.currentTab = 0;
                    }
                }
            }
        }

        EmptyState {
            anchors.centerIn: parent
            visible: view.detail.snapshots.length === 0
            glyph: Icons.history
            title: qsTr("No snapshots yet")
            text: qsTr("Every analysis is stored here, including failed ones.")
        }
    }

    component ActivityList: GlassPanel {
        radius: Theme.radiusCard
        clip: true

        ListView {
            anchors.fill: parent
            anchors.margins: Theme.spacingL
            clip: true
            spacing: 0
            boundsBehavior: Flickable.StopAtBounds
            model: view.detail.activity

            delegate: Item {
                id: eventRow

                required property var modelData
                required property int index

                width: ListView.view.width
                height: Math.max(48, messageText.implicitHeight + 26)

                Rectangle {
                    x: 7
                    y: eventRow.index === 0 ? 18 : 0
                    width: 2
                    height: parent.height - (eventRow.index === 0 ? 18 : 0)
                    color: Qt.rgba(1, 1, 1, 0.08)
                }

                Rectangle {
                    x: 2
                    y: 14
                    width: 12
                    height: 12
                    radius: 6
                    color: Theme.withAlpha(Theme.accent, eventRow.modelData.type.indexOf("failed") >= 0 ? 0 : 0.85)
                    border.width: 2
                    border.color: eventRow.modelData.type.indexOf("failed") >= 0 ? Theme.danger : Theme.accent
                }

                Text {
                    id: messageText

                    x: 30
                    y: 11
                    width: parent.width - 30 - timeText.width - Theme.spacingL
                    text: eventRow.modelData.message
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap
                }

                Text {
                    id: timeText

                    anchors.right: parent.right
                    y: 12
                    text: Format.relativeTime(eventRow.modelData.created_at)
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption + 1
                }
            }
        }
    }
}
