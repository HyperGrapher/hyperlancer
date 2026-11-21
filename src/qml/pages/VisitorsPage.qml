import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hyperlancer

// Live visitors of the portfolio site, read from its Convex backend: recent sessions or
// visitors, a switch for bots, and a detail view per visitor. The data keeps flowing in the
// background, so this page only shows it.
PageBase {
    id: page

    required property AppContext app

    readonly property VisitorStats visitors: app.visitors
    readonly property string status: visitors.status
    property int tab: 0

    signal settingsRequested

    function ago(ms) {
        const seconds = Math.max(0, Math.round((visitors.nowMs - ms) / 1000));
        if (seconds < 90)
            return qsTr("just now");
        const minutes = Math.round(seconds / 60);
        if (minutes < 60)
            return qsTr("%1 min ago").arg(minutes);
        const hours = Math.round(minutes / 60);
        if (hours < 24)
            return qsTr("%1 h ago").arg(hours);
        return Format.shortDate(ms);
    }

    function where(session) {
        const parts = [session.city, session.countryCode || session.country].filter(part => part && part.length > 0);
        return parts.length > 0 ? parts.join(", ") : qsTr("Unknown");
    }

    function device(session) {
        return [session.deviceType, session.browser, session.os].filter(part => part && part.length > 0).join(" · ");
    }

    function isLive(session) {
        return !session.isBot && visitors.nowMs - session.lastActivityAt < 75000;
    }

    readonly property string statusText: status === "live" ? qsTr("Live")
                                       : status === "connecting" ? qsTr("Connecting…")
                                       : status === "offline" ? qsTr("Offline, retrying")
                                       : qsTr("Not set up")
    readonly property color statusColor: status === "live" ? Theme.success
                                       : status === "offline" ? Theme.danger : Theme.textMuted

    PageHeader {
        id: header

        width: parent.width
        title: qsTr("Visitors")
        subtitle: page.status === "notConfigured" ? qsTr("Connect your portfolio site's Convex backend to see visitors here.")
                  : page.visitors.error.length > 0 ? page.visitors.error
                  : qsTr("%n on the site now. Updates arrive as they happen.", "", page.visitors.liveCount)
        opacity: page.reveal(0)
        transform: Translate { y: page.revealShift(0) }
        visible: page.visitors.detailVisitorId.length === 0

        Row {
            spacing: Theme.spacingM

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                StatusDot {
                    anchors.verticalCenter: parent.verticalCenter
                    color: page.statusColor
                    active: page.status === "live"
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.statusText
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }
            }

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingS

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.visitors.botSessionCount > 0 ? qsTr("Bots (%1)").arg(page.visitors.botSessionCount) : qsTr("Bots")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }

                ToggleSwitch {
                    anchors.verticalCenter: parent.verticalCenter
                    checked: page.visitors.showBots
                    onToggled: page.visitors.showBots = checked
                }
            }

            SegmentedControl {
                implicitHeight: Theme.inputHeight
                options: [qsTr("Sessions"), qsTr("Visitors")]
                currentIndex: page.tab
                onActivated: index => page.tab = index
            }
        }
    }

    GlassPanel {
        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingXl
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: page.visitors.detailVisitorId.length === 0
        radius: Theme.radiusCard
        clip: true
        opacity: page.reveal(1)
        transform: Translate { y: page.revealShift(1) }

        // Column captions; widths match the row cells below.
        Item {
            id: captions

            width: parent.width
            height: 34
            visible: list.count > 0

            Row {
                x: Theme.spacingL
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingM

                Repeater {
                    model: page.tab === 0
                           ? [[qsTr("When"), 120], [qsTr("Where"), 170], [qsTr("Site"), 140], [qsTr("Device"), 180], [qsTr("Landing"), 130], [qsTr("Pages"), 50], [qsTr("Source"), 90]]
                           : [[qsTr("Last seen"), 120], [qsTr("Where"), 170], [qsTr("Site"), 140], [qsTr("Device"), 180], [qsTr("Visits"), 60], [qsTr("First seen"), 130], [qsTr(""), 70]]

                    delegate: Text {
                        required property var modelData

                        width: modelData[1]
                        text: modelData[0].toUpperCase()
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1.1
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.glassStroke
            }
        }

        ListView {
            id: list

            anchors.top: captions.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: page.tab === 0 ? page.visitors.sessions : page.visitors.visitors

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                id: row

                required property var modelData
                // A visitors row carries { visitor, latest }; a sessions row is the session itself.
                readonly property var session: page.tab === 0 ? modelData : modelData.latest
                readonly property var visitor: page.tab === 0 ? null : modelData.visitor

                width: list.width
                height: 46

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: Theme.radiusSmall
                    color: rowArea.containsMouse ? Theme.rowHover : "transparent"
                }

                MouseArea {
                    id: rowArea

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.visitors.openVisitor(row.session.visitorId, row.session._id)
                }

                Row {
                    x: Theme.spacingL - 6
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingM

                    Text {
                        width: 120
                        text: page.ago(page.tab === 0 ? row.session.startedAt : row.visitor.lastSeen)
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }

                    Text {
                        width: 170
                        text: page.where(row.session)
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }

                    Text {
                        width: 140
                        text: row.session.origin || ""
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }

                    Text {
                        width: 180
                        text: page.device(row.session)
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }

                    Text {
                        width: page.tab === 0 ? 130 : 60
                        text: page.tab === 0 ? row.session.landingPath : String(row.visitor.visitCount)
                        horizontalAlignment: page.tab === 0 ? Text.AlignLeft : Text.AlignRight
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }

                    Text {
                        width: page.tab === 0 ? 50 : 130
                        text: page.tab === 0 ? String(row.session.pageviews) : page.ago(row.visitor.firstSeen)
                        horizontalAlignment: page.tab === 0 ? Text.AlignRight : Text.AlignLeft
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }

                    Text {
                        width: 90
                        visible: page.tab === 0
                        text: page.tab === 0 ? row.session.referrerType : ""
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }

                    Row {
                        spacing: 6
                        anchors.verticalCenter: parent.verticalCenter

                        Badge {
                            visible: page.isLive(row.session)
                            text: qsTr("live")
                            tint: Theme.success
                        }

                        Badge {
                            visible: row.session.isBot
                            text: qsTr("bot")
                            tint: Theme.danger
                        }

                        Badge {
                            visible: page.tab === 0 && row.session.isNewVisitor && !row.session.isBot
                            text: qsTr("new")
                            tint: Theme.accent
                        }
                    }
                }
            }
        }

        EmptyState {
            anchors.centerIn: parent
            width: parent.width
            visible: list.count === 0
            glyph: Icons.users
            title: page.status === "notConfigured" ? qsTr("Not set up yet")
                   : !page.visitors.loaded ? qsTr("Loading visitors…")
                   : qsTr("No visitors yet")
            text: page.status === "notConfigured" ? qsTr("Add the Convex address and the access key in Settings.")
                  : !page.visitors.loaded ? qsTr("Waiting for the first data from the site's backend.")
                  : page.visitors.showBots ? qsTr("Nobody has visited recently.") : qsTr("No people visited recently. Switch on Bots to see automated visits too.")

            SecondaryButton {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.bottom
                anchors.topMargin: Theme.spacingL
                visible: page.status === "notConfigured"
                text: qsTr("Open Settings")
                glyph: Icons.gearSix
                onClicked: page.settingsRequested()
            }
        }
    }

    VisitorDetail {
        anchors.fill: parent
        visible: page.visitors.detailVisitorId.length > 0
        app: page.app
    }
}
