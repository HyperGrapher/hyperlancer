import QtQuick
import QtQuick.Controls
import Hyperlancer

// One message opened full size, like Gmail's reading view: subject, sender and recipients,
// date, then the text. The body is fetched when the view opens and is never stored. It is
// shown as plain text, so nothing in a message can load images or track the reader.
GlassPanel {
    id: view

    required property AppContext app
    property var message: ({})

    property string body: ""
    property string bodyError: ""
    readonly property string gmailId: message.gmailId || ""
    readonly property bool loading: body.length === 0 && bodyError.length === 0

    signal backRequested

    function senderName(header) {
        const angle = header.indexOf("<");
        const name = (angle > 0 ? header.substring(0, angle) : header).replace(/"/g, "").trim();
        return name.length > 0 ? name : header;
    }

    radius: Theme.radiusCard
    clip: true
    onGmailIdChanged: {
        body = "";
        bodyError = "";
        if (gmailId.length > 0)
            app.gmail.loadMessageBody(gmailId);
    }

    Connections {
        target: view.app.gmail

        function onMessageBodyLoaded(gmailId, text, error) {
            if (gmailId !== view.gmailId)
                return;
            view.body = text;
            view.bodyError = error;
        }
    }

    Item {
        id: bar

        width: parent.width
        height: 52

        IconButton {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingM
            anchors.verticalCenter: parent.verticalCenter
            glyph: Icons.arrowLeft
            hint: qsTr("Back (Esc)")
            onClicked: view.backRequested()
        }

        IconButton {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingM
            anchors.verticalCenter: parent.verticalCenter
            glyph: Icons.openExternal
            hint: qsTr("Open in Gmail")
            onClicked: view.app.gmail.openThread(view.message.threadId)
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.glassStroke
        }
    }

    Flickable {
        id: scroller

        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingXl
        contentWidth: width
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: content

            width: scroller.width
            spacing: Theme.spacingL

            Text {
                width: parent.width
                text: view.message.subject || qsTr("(no subject)")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }

            Item {
                width: parent.width
                height: 46

                Rectangle {
                    id: avatar

                    width: 40
                    height: 40
                    radius: 20
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.withAlpha(Theme.accent, 0.2)

                    Text {
                        anchors.centerIn: parent
                        text: Format.initialOf(view.senderName(view.message.from || "?"))
                        color: Theme.accent
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontHeading
                        font.weight: Font.Bold
                    }
                }

                Column {
                    anchors.left: avatar.right
                    anchors.leftMargin: Theme.spacingM
                    anchors.right: dateLabel.left
                    anchors.rightMargin: Theme.spacingM
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Text {
                        width: parent.width
                        text: view.message.from || ""
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody + 1
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }

                    Text {
                        width: parent.width
                        text: qsTr("to %1").arg(view.message.to || "")
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption + 1
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                }

                Text {
                    id: dateLabel

                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: Format.dateTime(view.message.sentAt)
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption + 1
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.glassStroke
            }

            Text {
                width: parent.width
                text: view.loading ? qsTr("Loading…") : view.bodyError.length > 0 ? view.bodyError : view.body
                color: view.bodyError.length > 0 ? Theme.danger : Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody + 1
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                lineHeight: 1.25
            }
        }
    }
}
