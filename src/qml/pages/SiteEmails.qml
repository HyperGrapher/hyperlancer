import QtQuick
import QtQuick.Layouts
import Hyperlancer

// Emails exchanged with the site's domain, read from Gmail (read-only). Newest first, one
// row per message; clicking a row opens the message full size, the button on the right opens
// the thread in Gmail.
Item {
    id: panel

    required property AppContext app

    readonly property SiteDetail detail: app.siteDetail
    readonly property var messages: detail.emails
    property var openMessage: null

    function resetScroll() {
        openMessage = null;
        list.positionViewAtBeginning();
    }

    onVisibleChanged: if (!visible) openMessage = null

    GlassPanel {
        anchors.fill: parent
        radius: Theme.radiusCard
        clip: true
        visible: panel.openMessage === null

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

            delegate: MailRow {
                required property var modelData

                width: list.width
                message: modelData
                onOpened: panel.openMessage = modelData
                onGmailRequested: panel.app.gmail.openThread(modelData.threadId)
            }
        }

        EmptyState {
            anchors.centerIn: parent
            width: parent.width
            visible: panel.messages.length === 0 && !panel.detail.emailsLoading
            glyph: panel.app.gmail.connected ? Icons.tray : Icons.googleLogo
            title: !panel.app.gmail.connected ? qsTr("Connect Gmail to see emails")
                   : panel.detail.emailsError.length > 0 ? qsTr("Could not load emails") : qsTr("No emails with this domain yet")
            text: !panel.app.gmail.connected ? qsTr("Settings → Connections → Gmail. Access is read-only: the app never sends or changes mail.")
                  : panel.detail.emailsError.length > 0 ? panel.detail.emailsError
                  : qsTr("Messages sent to or received from this domain appear here.")
        }
    }

    MailView {
        anchors.fill: parent
        app: panel.app
        visible: panel.openMessage !== null
        message: panel.openMessage || ({})
        onBackRequested: panel.openMessage = null
    }
}
