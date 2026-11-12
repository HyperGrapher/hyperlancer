import QtQuick

// Slim progress bar with the accent gradient. A negative value shows a travelling shimmer
// for "working, amount unknown".
Item {
    id: bar

    property real value: 0
    readonly property bool indeterminate: value < 0

    implicitHeight: 4
    implicitWidth: 120
    clip: true

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Qt.rgba(1, 1, 1, 0.08)
    }

    Rectangle {
        height: parent.height
        radius: height / 2
        width: bar.indeterminate ? parent.width * 0.32 : parent.width * Math.max(0, Math.min(1, bar.value))
        x: bar.indeterminate ? shimmer.position * (parent.width * 1.32) - parent.width * 0.32 : 0
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.accent }
            GradientStop { position: 1; color: Theme.accentSecondary }
        }

        Behavior on width {
            enabled: !bar.indeterminate
            NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic }
        }
    }

    QtObject {
        id: shimmer

        property real position: 0
    }

    NumberAnimation {
        target: shimmer
        property: "position"
        from: 0
        to: 1
        duration: 1300
        loops: Animation.Infinite
        running: bar.indeterminate && bar.visible && !Motion.reducedMotion
        easing.type: Easing.InOutQuad
    }
}
