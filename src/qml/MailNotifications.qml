import QtQuick
import QtQuick.Layouts
import Hyperlancer

// New-mail notices in a small always-on-top window in the screen corner. Unlike a system
// balloon, a notice stays until the user clicks Dismiss, opens the message, or looks at the
// Emails page. The window exists only while there is something to show.
Window {
    id: popup

    required property AppContext app
    required property AppSettings appSettings

    // Newest first: { gmailId, from, subject, snippet }
    property var pending: []
    readonly property int maxShown: 4
    readonly property int cardHeight: 92
    readonly property int headerHeight: pending.length > 1 ? 38 : 0

    function senderName(header) {
        const angle = header.indexOf("<");
        const name = (angle > 0 ? header.substring(0, angle) : header).replace(/"/g, "").trim();
        return name.length > 0 ? name : header;
    }

    function dismiss(gmailId) {
        pending = pending.filter(message => message.gmailId !== gmailId);
    }

    function openEmails() {
        pending = [];
        app.requestOpenEmails();
    }

    // Not a child of the main window: it must stay visible while that one is hidden in the tray.
    transientParent: null
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: pending.length > 0
    width: 392
    height: Math.min(pending.length, maxShown) * (cardHeight + 8) + headerHeight + 16
    x: Screen.virtualX + Screen.desktopAvailableWidth - width - 8
    y: Screen.virtualY + Screen.desktopAvailableHeight - height - 8

    Connections {
        target: popup.app.inbox

        function onNewMessages(messages) {
            if (!popup.appSettings.notifyNewMail)
                return;
            const fresh = messages.map(message => ({
                gmailId: message.gmailId,
                from: message.from,
                subject: message.subject,
                snippet: message.snippet
            }));
            popup.pending = fresh.concat(popup.pending);
        }

        // The Emails page was opened (or the badge cleared elsewhere): the user has seen them.
        function onNewCountChanged() {
            if (popup.app.inbox.newCount === 0)
                popup.pending = [];
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
                text: qsTr("%n new emails", "", popup.pending.length)
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
                    id: mailIcon

                    x: Theme.spacingM
                    y: Theme.spacingM
                    glyph: Icons.envelope
                    size: 18
                    color: Theme.accent
                }

                Column {
                    anchors.left: mailIcon.right
                    anchors.leftMargin: Theme.spacingM
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingM
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spacingM - 2
                    spacing: 2

                    Text {
                        width: parent.width
                        text: popup.senderName(card.modelData.from)
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody + 1
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }

                    Text {
                        width: parent.width
                        text: card.modelData.subject || qsTr("(no subject)")
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
                        onClicked: popup.dismiss(card.modelData.gmailId)
                    }

                    PrimaryButton {
                        implicitHeight: 30
                        implicitWidth: 96
                        text: qsTr("Open")
                        onClicked: popup.openEmails()
                    }
                }
            }
        }
    }
}
