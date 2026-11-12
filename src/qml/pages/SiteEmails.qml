import QtQuick
import QtQuick.Layouts
import Hyperlancer

// Emails exchanged with the site's domain, read from Gmail (read-only). Newest first, one
// row per message; opening a row shows it in Gmail in the browser.
GlassPanel {
    id: panel

    required property AppContext app

    readonly property SiteDetail detail: app.siteDetail
    readonly property var messages: detail.emails

    radius: Theme.radiusCard
    clip: true

    Item {
        id: toolbar

        width: parent.width
        height: 52

        Text {
            x: Theme.spacingL + 2
            anchors.verticalCenter: parent.verticalCenter
            text: panel.app.gmail.connected ? qsTr("%n message(s) with this domain", "", panel.messages.length)
                                            : qsTr("Gmail is not connected")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontHeading
            font.weight: Font.DemiBold
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingM
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingS

            ThinProgress {
                anchors.verticalCenter: parent.verticalCenter
                width: 80
                value: -1
                visible: panel.detail.emailsLoading
            }

            SecondaryButton {
                text: qsTr("Sync from Gmail")
                glyph: Icons.refresh
                enabled: panel.app.gmail.connected && !panel.detail.emailsLoading
                onClicked: panel.detail.loadEmails()
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

        anchors.top: toolbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 6
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: panel.messages

        delegate: Item {
            id: message

            required property var modelData
            readonly property bool sent: modelData.direction === "sent"

            width: list.width
            height: 64

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusSmall
                color: hover.hovered ? Theme.rowHover : "transparent"
            }

            Rectangle {
                id: directionBadge

                x: Theme.spacingM
                anchors.verticalCenter: parent.verticalCenter
                width: 32
                height: 32
                radius: 10
                color: Theme.withAlpha(message.modelData.isBounce ? Theme.danger : message.sent ? Theme.accent : Theme.info, 0.15)

                Icon {
                    anchors.centerIn: parent
                    glyph: message.modelData.isBounce ? Icons.warningCircle : message.sent ? Icons.paperPlane : Icons.replyArrow
                    size: 16
                    color: message.modelData.isBounce ? Theme.danger : message.sent ? Theme.accent : Theme.info
                }
            }

            Column {
                anchors.left: directionBadge.right
                anchors.leftMargin: Theme.spacingM
                anchors.right: dateText.left
                anchors.rightMargin: Theme.spacingL
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3

                Row {
                    width: parent.width
                    spacing: Theme.spacingS

                    Text {
                        id: subjectText

                        width: Math.min(implicitWidth, parent.width - (autoBadge.visible ? autoBadge.width + 8 : 0) - (clip.visible ? 22 : 0))
                        text: message.modelData.subject || qsTr("(no subject)")
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody + 1
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Icon {
                        id: clip

                        glyph: Icons.paperclip
                        size: 13
                        color: Theme.textMuted
                        visible: message.modelData.hasAttachments
                        anchors.verticalCenter: subjectText.verticalCenter
                    }

                    Badge {
                        id: autoBadge

                        anchors.verticalCenter: subjectText.verticalCenter
                        visible: message.modelData.isAutoReply || message.modelData.isBounce
                        text: message.modelData.isBounce ? qsTr("Bounced") : qsTr("Auto-reply")
                        tint: message.modelData.isBounce ? Theme.danger : Theme.textMuted
                    }
                }

                Text {
                    width: parent.width
                    text: (message.sent ? qsTr("To ") + message.modelData.to : qsTr("From ") + message.modelData.from)
                          + "  —  " + message.modelData.snippet
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption + 1
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
            }

            Text {
                id: dateText

                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingL
                anchors.verticalCenter: parent.verticalCenter
                text: Format.shortDate(message.modelData.sentAt)
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption + 1
            }

            Rectangle {
                anchors.left: directionBadge.right
                anchors.leftMargin: Theme.spacingM
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.rowDivider
            }

            HoverHandler {
                id: hover

                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: panel.app.gmail.openThread(message.modelData.threadId)
            }
        }
    }

    EmptyState {
        anchors.centerIn: list
        width: list.width
        visible: panel.messages.length === 0 && !panel.detail.emailsLoading
        glyph: panel.app.gmail.connected ? Icons.tray : Icons.googleLogo
        title: !panel.app.gmail.connected ? qsTr("Connect Gmail to see emails")
               : panel.detail.emailsError.length > 0 ? qsTr("Could not load emails") : qsTr("No emails with this domain yet")
        text: !panel.app.gmail.connected ? qsTr("Settings → Connections → Gmail. Access is read-only: the app never sends or changes mail.")
              : panel.detail.emailsError.length > 0 ? panel.detail.emailsError
              : qsTr("Messages sent to or received from this domain appear here.")
    }
}
