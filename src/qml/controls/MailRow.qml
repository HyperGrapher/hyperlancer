import QtQuick
import Hyperlancer

// One message in a list: direction icon, subject, correspondent and snippet, date, and a
// button that opens the thread in Gmail. Unread messages are bold.
Item {
    id: row

    required property var message
    readonly property bool sent: message.direction === "sent"

    signal opened
    signal gmailRequested

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
        color: Theme.withAlpha(row.message.isBounce ? Theme.danger : row.sent ? Theme.accent : Theme.info, 0.15)

        Icon {
            anchors.centerIn: parent
            glyph: row.message.isBounce ? Icons.warningCircle : row.sent ? Icons.paperPlane : Icons.replyArrow
            size: 16
            color: row.message.isBounce ? Theme.danger : row.sent ? Theme.accent : Theme.info
        }

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: -2
            anchors.topMargin: -2
            width: 9
            height: 9
            radius: 5
            color: Theme.accent
            border.width: 2
            border.color: Qt.rgba(0.05, 0.03, 0.1, 0.9)
            visible: row.message.unread === true
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
                text: row.message.subject || qsTr("(no subject)")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody + 1
                font.weight: row.message.unread === true ? Font.Bold : Font.DemiBold
                elide: Text.ElideRight
            }

            Icon {
                id: clip

                glyph: Icons.paperclip
                size: 13
                color: Theme.textMuted
                visible: row.message.hasAttachments
                anchors.verticalCenter: subjectText.verticalCenter
            }

            Badge {
                id: autoBadge

                anchors.verticalCenter: subjectText.verticalCenter
                visible: row.message.isAutoReply || row.message.isBounce
                text: row.message.isBounce ? qsTr("Bounced") : qsTr("Auto-reply")
                tint: row.message.isBounce ? Theme.danger : Theme.textMuted
            }
        }

        Text {
            width: parent.width
            text: (row.sent ? qsTr("To ") + row.message.to : qsTr("From ") + row.message.from) + "  —  " + row.message.snippet
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption + 1
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }
    }

    Text {
        id: dateText

        anchors.right: openButton.left
        anchors.rightMargin: Theme.spacingM
        anchors.verticalCenter: parent.verticalCenter
        text: Format.shortDate(row.message.sentAt)
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontCaption + 1
    }

    IconButton {
        id: openButton

        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingM
        anchors.verticalCenter: parent.verticalCenter
        glyph: Icons.openExternal
        flat: true
        hint: qsTr("Open in Gmail")
        onClicked: row.gmailRequested()
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
        // The Gmail button handles its own taps.
        onTapped: eventPoint => {
            if (!openButton.contains(openButton.mapFromItem(row, eventPoint.position)))
                row.opened();
        }
    }
}
