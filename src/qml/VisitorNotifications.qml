import QtQuick
import QtQuick.Layouts
import Hyperlancer

// A notice for each new person who visits the portfolio site, in a small always-on-top window
// in the top-right corner (the mail notices use the bottom-right). It stays until dismissed.
Window {
    id: popup

    required property AppContext app

    // Newest first: { sessionId, visitorId, place, device, landing, returning }
    property var pending: []
    readonly property int maxShown: 4
    readonly property int cardHeight: 92
    readonly property int headerHeight: pending.length > 1 ? 38 : 0

    signal openRequested(string visitorId, string sessionId)

    function dismiss(sessionId) {
        pending = pending.filter(item => item.sessionId !== sessionId);
    }

    transientParent: null
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: pending.length > 0
    width: 392
    height: Math.min(pending.length, maxShown) * (cardHeight + 8) + headerHeight + 16
    x: Screen.virtualX + Screen.desktopAvailableWidth - width - 8
    y: Screen.virtualY + 8

    Connections {
        target: popup.app.visitors

        function onNewVisitor(session) {
            const place = [session.city, session.country].filter(part => part && part.length > 0).join(", ");
            const device = [session.deviceType, session.browser, session.os].filter(part => part && part.length > 0).join(" · ");
            const item = {
                sessionId: session._id,
                visitorId: session.visitorId,
                place: place.length > 0 ? place : qsTr("Unknown place"),
                device: device,
                site: session.origin,
                landing: session.landingPath,
                returning: !session.isNewVisitor
            };
            popup.pending = [item].concat(popup.pending).slice(0, 20);
        }
    }

    Column {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        Rectangle {
            width: parent.width
            height: popup.headerHeight
            visible: popup.headerHeight > 0
            radius: Theme.radiusControl
            color: Qt.rgba(0.07, 0.05, 0.12, 0.97)
            border.width: 1
            border.color: Theme.glassStroke

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingS
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("%n new visitors", "", popup.pending.length)
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody + 1
                font.weight: Font.DemiBold
            }

            SecondaryButton {
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                implicitHeight: 28
                text: qsTr("Dismiss all")
                onClicked: popup.pending = []
            }
        }

        Repeater {
            model: popup.pending.slice(0, popup.maxShown)

            delegate: Rectangle {
                id: card

                required property var modelData

                width: parent.width
                height: popup.cardHeight
                radius: Theme.radiusCard
                color: Qt.rgba(0.07, 0.05, 0.12, 0.97)
                border.width: 1
                border.color: Theme.withAlpha(Theme.accent, 0.5)

                Icon {
                    id: icon

                    x: Theme.spacingM
                    y: Theme.spacingM
                    glyph: Icons.users
                    size: 18
                    color: Theme.accent
                }

                Column {
                    anchors.left: icon.right
                    anchors.leftMargin: Theme.spacingM
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingM
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spacingM - 2
                    spacing: 2

                    Text {
                        width: parent.width
                        text: (card.modelData.returning ? qsTr("Returning visitor") : qsTr("New visitor")) + "  ·  " + card.modelData.place
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody + 1
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }

                    Text {
                        width: parent.width
                        text: [card.modelData.site, card.modelData.device, card.modelData.landing].filter(part => part && part.length > 0).join("  ·  ")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingM
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.spacingS
                    spacing: Theme.spacingS

                    SecondaryButton {
                        implicitHeight: 30
                        text: qsTr("Dismiss")
                        onClicked: popup.dismiss(card.modelData.sessionId)
                    }

                    PrimaryButton {
                        implicitHeight: 30
                        implicitWidth: 96
                        text: qsTr("Open")
                        onClicked: {
                            popup.openRequested(card.modelData.visitorId, card.modelData.sessionId);
                            popup.dismiss(card.modelData.sessionId);
                        }
                    }
                }
            }
        }
    }
}
