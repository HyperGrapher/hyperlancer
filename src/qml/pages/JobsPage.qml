import QtQuick
import QtQuick.Layouts
import Hyperlancer

// Background jobs: what runs now, what waits, and how earlier jobs ended. Running rows show
// live progress; finished rows can be retried or their log opened.
PageBase {
    id: page

    required property AppContext app
    property bool ambientActive: true

    readonly property int running: app.jobRunner.runningCount
    readonly property int active: app.jobs.activeCount

    signal siteOpened(var siteId)

    function statusLabel(status) {
        switch (status) {
        case "queued": return qsTr("Queued");
        case "running": return qsTr("Running");
        case "succeeded": return qsTr("Done");
        case "partial": return qsTr("Partial");
        case "failed": return qsTr("Failed");
        case "canceled": return qsTr("Canceled");
        default: return status;
        }
    }

    onEntered: app.jobs.refresh()

    PageHeader {
        id: header

        width: parent.width
        title: qsTr("Jobs")
        subtitle: qsTr("Analyses run in the background, up to three at a time. Each one has a 2-minute limit.")
        opacity: page.reveal(0)
        transform: Translate { y: page.revealShift(0) }
    }

    StatusCard {
        id: statusCard

        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingXl
        width: parent.width
        status: page.running > 0 ? "running" : page.active > 0 ? "paused" : "idle"
        detail: page.running > 0 ? qsTr("%n analysis(es) running", "", page.running)
                                   + (page.active - page.running > 0 ? qsTr(", %n waiting", "", page.active - page.running) : "")
                : page.active > 0 ? qsTr("%n job(s) waiting to start", "", page.active)
                : qsTr("Nothing is running. Analyses you start appear here.")
        ambientActive: page.ambientActive
        opacity: page.reveal(1)
        transform: Translate { y: page.revealShift(1) }
    }

    GlassPanel {
        anchors.top: statusCard.bottom
        anchors.topMargin: Theme.spacingL
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        radius: Theme.radiusCard
        clip: true
        opacity: page.reveal(2)
        transform: Translate { y: page.revealShift(2) }

        TableHeader {
            id: jobsHeader

            width: parent.width
            horizontalPadding: Theme.spacingL + 2

            TableHeader.Caption { text: qsTr("Site"); Layout.fillWidth: true; Layout.leftMargin: 26 }
            TableHeader.Caption { text: qsTr("Job"); Layout.preferredWidth: 80 }
            TableHeader.Caption { text: qsTr("Status"); Layout.preferredWidth: 96 }
            TableHeader.Caption { text: qsTr("Progress / result"); Layout.preferredWidth: 260 }
            TableHeader.Caption { text: qsTr("Started"); Layout.preferredWidth: 110 }
            Item { Layout.preferredWidth: 76 }
        }

        ListView {
            id: list

            anchors.top: jobsHeader.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: page.app.jobs

            delegate: Item {
                id: job

                required property var jobId
                required property var siteId
                required property string siteKey
                required property string type
                required property string status
                required property real progress
                required property string progressLabel
                required property string error
                required property string createdAt
                required property string startedAt
                required property bool isActive

                width: list.width
                height: Theme.rowHeight

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSmall
                    color: hover.hovered ? Theme.rowHover : "transparent"
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingL - 4
                    anchors.rightMargin: Theme.spacingL - 4
                    spacing: Theme.spacingM

                    StatusDot {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.leftMargin: 4
                        color: Theme.statusColor(job.status)
                        active: job.status === "running"
                        size: 9
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        text: job.siteKey || qsTr("(deleted site)")
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody + 1
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.preferredWidth: 80
                        text: job.type === "profile" ? qsTr("Analysis") : job.type
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                    }

                    Item {
                        Layout.preferredWidth: 96
                        Layout.preferredHeight: 22

                        Badge {
                            anchors.verticalCenter: parent.verticalCenter
                            text: page.statusLabel(job.status)
                            tint: Theme.statusColor(job.status)
                        }
                    }

                    Item {
                        Layout.preferredWidth: 260
                        Layout.fillHeight: true

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            spacing: 5
                            visible: job.isActive

                            Text {
                                text: job.status === "queued" ? qsTr("Waiting for a free slot") : (job.progressLabel || qsTr("Starting"))
                                color: Theme.accent
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontCaption + 1
                            }

                            ThinProgress {
                                width: parent.width - 20
                                value: job.status === "queued" ? -1 : Math.max(0.04, job.progress)
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            visible: !job.isActive
                            text: job.error.length > 0 ? job.error : job.status === "canceled" ? qsTr("Stopped by you") : qsTr("Snapshot stored")
                            color: job.error.length > 0 ? Theme.danger : Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontCaption + 1
                            elide: Text.ElideRight
                        }
                    }

                    Text {
                        Layout.preferredWidth: 110
                        text: Format.relativeTime(job.startedAt || job.createdAt)
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                    }

                    Row {
                        Layout.preferredWidth: 76
                        layoutDirection: Qt.RightToLeft
                        spacing: 2

                        IconButton {
                            flat: true
                            visible: job.isActive
                            glyph: Icons.stop
                            danger: true
                            hint: qsTr("Cancel")
                            onClicked: page.app.jobRunner.cancel(job.jobId)
                        }

                        IconButton {
                            flat: true
                            visible: !job.isActive
                            glyph: Icons.refresh
                            hint: qsTr("Retry")
                            onClicked: page.app.jobRunner.retry(job.jobId)
                        }

                        IconButton {
                            flat: true
                            glyph: Icons.fileText
                            hint: qsTr("Open log")
                            onClicked: page.app.openJobLog(job.jobId)
                        }
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: Theme.spacingL + 22
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.rowDivider
                }

                HoverHandler {
                    id: hover

                    cursorShape: job.siteId > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                }

                TapHandler {
                    enabled: job.siteId > 0
                    onTapped: page.siteOpened(job.siteId)
                }
            }
        }

        EmptyState {
            anchors.centerIn: list
            width: list.width
            visible: page.app.jobs.count === 0
            glyph: Icons.queue
            title: qsTr("No jobs yet")
            text: qsTr("Analyze a site from Dashboard or Websites, or add leads from Discover.")
        }
    }
}
