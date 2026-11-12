import QtQuick
import QtQuick.Layouts
import Hyperlancer

// The desktop and mobile screenshots saved by the last speed check (Lighthouse's view of the
// page). They live in the app data folder, in screenshots/<site id>/.
Item {
    id: panel

    property var screenshots: ({})
    property bool running: false

    readonly property bool hasAny: screenshots.desktop !== undefined || screenshots.mobile !== undefined

    signal checkRequested

    RowLayout {
        anchors.fill: parent
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
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: !panel.hasAny
        glyph: Icons.deviceMobile
        title: panel.running ? qsTr("Taking screenshots…") : qsTr("No screenshots yet")
        text: qsTr("A speed check saves a desktop and a mobile screenshot. Use the speed button at the top of this page.")
    }

    component Shot: GlassPanel {
        id: card

        property string title: ""
        property var shot: undefined

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

        Image {
            anchors.top: caption.bottom
            anchors.topMargin: Theme.spacingM
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacingM
            source: card.shot ? card.shot.url : ""
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: false // the file is replaced by the next check
            sourceSize.width: 1200
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
