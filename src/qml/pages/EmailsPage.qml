import QtQuick
import Hyperlancer

// The inbox: messages still in Gmail's INBOX (not archived), newest first. New mail is
// checked for every minute in the background; clicking a message opens it full size.
PageBase {
    id: page

    required property AppContext app

    readonly property GmailInbox inbox: app.inbox
    property var openMessage: null

    // The user is looking at the inbox while the window is on screen.
    readonly property bool watching: isCurrent && Window.window !== null && Window.window.visible

    onEntered: {
        if (app.gmail.connected && inbox.messages.length === 0)
            inbox.refresh();
        inbox.markSeen();
    }
    onWatchingChanged: if (watching) inbox.markSeen()

    Connections {
        target: page.inbox

        // Mail that arrives while the inbox is open is seen at once.
        function onNewCountChanged() {
            if (page.watching)
                page.inbox.markSeen();
        }
    }
    Keys.onEscapePressed: openMessage = null

    PageHeader {
        id: header

        width: parent.width
        title: qsTr("Emails")
        subtitle: !page.app.gmail.connected ? qsTr("Connect Gmail in Settings to read your inbox here.")
                  : page.inbox.unreadCount > 0 ? qsTr("%n unread in your inbox", "", page.inbox.unreadCount)
                  : qsTr("Your inbox. New mail is checked every minute.")
        opacity: page.reveal(0)
        transform: Translate { y: page.revealShift(0) }

        SecondaryButton {
            implicitHeight: Theme.inputHeight
            text: qsTr("Refresh")
            glyph: Icons.refresh
            enabled: page.app.gmail.connected && !page.inbox.loading
            onClicked: page.inbox.refresh()
        }
    }

    GlassPanel {
        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingXl
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        radius: Theme.radiusCard
        clip: true
        visible: page.openMessage === null
        opacity: page.reveal(1)
        transform: Translate { y: page.revealShift(1) }

        ThinProgress {
            anchors.top: parent.top
            width: parent.width
            value: -1
            visible: page.inbox.loading
        }

        ListView {
            id: list

            anchors.fill: parent
            anchors.margins: 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: page.inbox.messages

            delegate: MailRow {
                required property var modelData

                width: list.width
                message: modelData
                onOpened: page.openMessage = modelData
                onGmailRequested: page.app.gmail.openThread(modelData.threadId)
            }
        }

        EmptyState {
            anchors.centerIn: parent
            width: parent.width
            visible: page.inbox.messages.length === 0 && !page.inbox.loading
            glyph: page.app.gmail.connected ? Icons.tray : Icons.googleLogo
            title: !page.app.gmail.connected ? qsTr("Gmail is not connected")
                   : page.inbox.error.length > 0 ? qsTr("Could not load the inbox") : qsTr("Inbox is empty")
            text: !page.app.gmail.connected ? qsTr("Settings → Connections → Gmail. Access is read-only: the app never sends or changes mail.")
                  : page.inbox.error.length > 0 ? page.inbox.error : qsTr("Nothing waiting in your inbox.")
        }
    }

    MailView {
        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingXl
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        app: page.app
        visible: page.openMessage !== null
        message: page.openMessage || ({})
        onBackRequested: page.openMessage = null
    }
}
