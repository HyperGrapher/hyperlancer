import QtQuick
import QtQuick.Layouts
import Hyperlancer

// Screenshots of the page: from a speed check (Lighthouse's view) or from the browser capture
// (desktop, mobile and the full page). They live in the app data folder, in screenshots/<site id>/.
Item {
    id: panel

    property var screenshots: ({})
    property bool running: false
    property bool capturing: false

    readonly property bool hasAny: screenshots.desktop !== undefined || screenshots.mobile !== undefined

    signal checkRequested
    signal captureRequested

    PrimaryButton {
        id: captureButton

        anchors.top: parent.top
        anchors.right: parent.right
        implicitHeight: 40
        implicitWidth: 200
        text: panel.capturing ? qsTr("Capturing…") : qsTr("Capture in browser")
        glyph: Icons.deviceMobile
        enabled: !panel.capturing
        onClicked: panel.captureRequested()
    }

    RowLayout {
        anchors.top: captureButton.bottom
        anchors.topMargin: Theme.spacingM
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: Theme.spacingM
        visible: panel.hasAny

        Shot {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 3
            title: qsTr("Desktop")
            shot: panel.screenshots.desktop
        }

        Shot {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1.4
            title: qsTr("Mobile")
            shot: panel.screenshots.mobile
        }

        Shot {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1.4
            visible: panel.screenshots.fullpage !== undefined
            title: qsTr("Full page")
            shot: panel.screenshots.fullpage
            scrolling: true
        }
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: !panel.hasAny
        glyph: Icons.deviceMobile
        title: panel.running ? qsTr("Taking screenshots…") : qsTr("No screenshots yet")
        text: qsTr("Capture the page in a browser, or run a speed check, which saves a desktop and a mobile screenshot too.")
    }

    component Shot: GlassPanel {
        id: card

        property string title: ""
        property var shot: undefined
        // A very tall picture (the full page) scrolls instead of shrinking to nothing.
        property bool scrolling: false

        radius: Theme.radiusCard
        clip: true

        Text {
            id: caption

            x: Theme.spacingL
            y: Theme.spacingM
            text: card.title + (card.shot ? "  ·  " + Format.relativeTime(card.shot.createdAt) : "")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontHeading
            font.weight: Font.DemiBold
        }

        Flickable {
            id: scroller

            anchors.top: caption.bottom
            anchors.topMargin: Theme.spacingM
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacingM
            clip: true
            contentWidth: width
            contentHeight: card.scrolling ? shotImage.height : height
            interactive: card.scrolling
            boundsBehavior: Flickable.StopAtBounds

            Image {
                id: shotImage

                width: scroller.width
                height: card.scrolling ? (implicitWidth > 0 ? width * implicitHeight / implicitWidth : 0) : scroller.height
                source: card.shot ? card.shot.url : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false // the file is replaced by the next check
                sourceSize.width: 1200
            }
        }

        Text {
            anchors.centerIn: parent
            visible: !card.shot
            text: qsTr("Not taken")
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
        }
    }
}
