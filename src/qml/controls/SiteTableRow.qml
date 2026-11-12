import QtQuick
import QtQuick.Layouts

// One row of the Sites table. Column widths match the captions in SitesPage. While an
// analysis runs, the "Analyzed" cell turns into a progress bar with the current step.
Item {
    id: row

    required property int index
    required property var siteId
    required property string siteKey
    required property string faviconUrl
    required property string status
    required property string category
    required property string tags
    required property int openTodos
    required property string url
    required property string title
    required property string deadlineOn
    required property string outcome
    required property string lastFetchedAt
    required property string jobStatus
    required property real progress
    required property string progressLabel

    property bool wide: true

    readonly property bool working: jobStatus === "running" || jobStatus === "queued"

    signal opened(var siteId)
    signal analyzeRequested(var siteId, string url)
    signal externalRequested(string url)
    signal statusRequested(var siteId, string status)

    height: Theme.rowHeight

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: Theme.radiusSmall
        color: hover.hovered ? Theme.rowHover : "transparent"

        Behavior on color { ColorAnimation { duration: Motion.fast } }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingL - 4
        anchors.rightMargin: Theme.spacingL - 4
        spacing: Theme.spacingM

        SiteBadge {
            siteKey: row.siteKey
            faviconUrl: row.faviconUrl
            status: row.working ? "running" : row.outcome
            size: 32
            Layout.alignment: Qt.AlignVCenter
        }

        Column {
            Layout.fillWidth: true
            Layout.minimumWidth: 156
            Layout.alignment: Qt.AlignVCenter
            spacing: 2

            Text {
                width: parent.width
                text: row.siteKey
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody + 1
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: row.title.length > 0 ? row.title : row.url
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption + 1
                elide: Text.ElideRight
            }
        }

        Item {
            id: statusCell

            Layout.preferredWidth: 124
            Layout.fillHeight: true

            Dropdown {
                id: statusDropdown

                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                compact: true
                options: Format.leadStatusOptions
                currentValue: row.status
                onActivated: value => row.statusRequested(row.siteId, value)
            }
        }

        Cell {
            Layout.preferredWidth: 104
            text: Format.orDash(row.category)
        }

        Cell {
            Layout.preferredWidth: 120
            visible: row.wide
            text: Format.orDash(row.tags)
            color: Theme.textMuted
        }

        Cell {
            Layout.preferredWidth: 84
            text: row.deadlineOn.length > 0 ? Format.shortDate(row.deadlineOn) : "—"
            color: row.deadlineOn.length > 0 ? Format.daysColor(Format.daysUntil(row.deadlineOn)) : Theme.textMuted
        }

        Cell {
            Layout.preferredWidth: 46
            text: row.openTodos > 0 ? String(row.openTodos) : "—"
            color: row.openTodos > 0 ? Theme.textPrimary : Theme.textMuted
            font.features: { "tnum": 1 }
        }

        Item {
            Layout.preferredWidth: 104
            Layout.fillHeight: true

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                visible: !row.working
                text: row.lastFetchedAt.length > 0 ? Format.relativeTime(row.lastFetchedAt) : qsTr("Never")
                color: row.outcome === "failed" ? Theme.danger : Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                elide: Text.ElideRight
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                spacing: 5
                visible: row.working

                Text {
                    width: parent.width
                    text: row.jobStatus === "queued" ? qsTr("Queued") : (row.progressLabel || qsTr("Analyzing"))
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption + 1
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }

                ThinProgress {
                    width: parent.width - 8
                    value: row.jobStatus === "queued" ? -1 : Math.max(0.04, row.progress)
                }
            }
        }

        Row {
            Layout.preferredWidth: 72
            Layout.alignment: Qt.AlignVCenter
            layoutDirection: Qt.RightToLeft
            spacing: 2
            opacity: hover.hovered ? 1 : 0

            Behavior on opacity { NumberAnimation { duration: Motion.fast } }

            IconButton {
                glyph: Icons.lightning
                flat: true
                hint: qsTr("Analyze again")
                enabled: !row.working
                onClicked: row.analyzeRequested(row.siteId, row.url)
            }

            IconButton {
                glyph: Icons.openExternal
                flat: true
                hint: qsTr("Open website")
                onClicked: row.externalRequested(row.url)
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.spacingL + 40
        anchors.rightMargin: Theme.spacingL - 4
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.rowDivider
    }

    HoverHandler {
        id: hover

        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        // The status dropdown handles its own taps.
        onTapped: eventPoint => {
            if (!statusDropdown.contains(statusDropdown.mapFromItem(row, eventPoint.position)))
                row.opened(row.siteId);
        }
    }

    component Cell: Text {
        Layout.alignment: Qt.AlignVCenter
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        elide: Text.ElideRight
    }
}
