import QtQuick

// Colored dot for a status. While "active" a ring breathes out of it (stops with ambient
// motion and in reduced-motion mode).
Item {
    id: dot

    property color color: Theme.textMuted
    property bool active: false
    property real size: 8

    implicitWidth: size
    implicitHeight: size

    Rectangle {
        id: ring

        anchors.centerIn: parent
        width: dot.size
        height: dot.size
        radius: width / 2
        color: "transparent"
        border.width: 1.5
        border.color: dot.color
        opacity: 0
    }

    ParallelAnimation {
        running: dot.active && dot.visible && !Motion.reducedMotion
        loops: Animation.Infinite
        onRunningChanged: if (!running) ring.opacity = 0

        NumberAnimation { target: ring; property: "scale"; from: 1; to: 2.6; duration: 1300; easing.type: Easing.OutCubic }
        NumberAnimation { target: ring; property: "opacity"; from: 0.7; to: 0; duration: 1300; easing.type: Easing.OutCubic }
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: dot.color

        Behavior on color { ColorAnimation { duration: Motion.normal } }
    }
}
