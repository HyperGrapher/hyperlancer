import QtQuick

// A site's initial on a soft tile tinted by a stable hue from its domain, with a small
// status dot in the corner (outcome of the last analysis).
Item {
    id: badge

    property string siteKey: ""
    property string status: ""   // "", ok, partial, failed, running
    property real size: 32
    property string faviconUrl: ""

    readonly property color tint: Qt.hsla(Format.hueOf(siteKey) / 360, 0.7, 0.74, 1)

    implicitWidth: size
    implicitHeight: size

    Rectangle {
        anchors.fill: parent
        radius: Math.round(badge.size * 0.32)
        color: Qt.rgba(0.04, 0.02, 0.08, 0.32)
        border.width: 1
        border.color: Theme.withAlpha(badge.tint, 0.45)

        Image {
            id: favicon

            anchors.centerIn: parent
            width: Math.round(badge.size * 0.56)
            height: width
            // Letter stays when the icon is off, missing or fails to load.
            source: Theme.showFavicons ? badge.faviconUrl : ""
            sourceSize: Qt.size(width * 2, height * 2)
            asynchronous: true
            fillMode: Image.PreserveAspectFit
            visible: status === Image.Ready
        }

        Text {
            anchors.centerIn: parent
            visible: !favicon.visible
            text: Format.initialOf(badge.siteKey)
            color: badge.tint
            font.family: Theme.fontFamily
            font.pixelSize: Math.round(badge.size * 0.44)
            font.weight: Font.Bold
        }
    }

    StatusDot {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: -2
        anchors.bottomMargin: -2
        size: 9
        visible: badge.status.length > 0
        color: Theme.statusColor(badge.status)
        active: badge.status === "running"

        Rectangle {
            anchors.centerIn: parent
            width: parent.width + 4
            height: width
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: Qt.rgba(0.05, 0.03, 0.1, 0.9)
            z: -1
        }
    }
}
