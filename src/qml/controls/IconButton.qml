import QtQuick
import QtQuick.Controls

// Square glass button with one icon and an optional tooltip. "flat" drops the resting
// background, for buttons inside rows.
AbstractButton {
    id: control

    property string glyph: Icons.cube
    property int iconSize: 16
    property bool flat: false
    property bool danger: false
    property string hint: ""

    implicitWidth: 34
    implicitHeight: 34
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : 0.35
    scale: down ? 0.92 : 1

    Behavior on scale { NumberAnimation { duration: Motion.fast; easing.type: Easing.OutCubic } }

    ToolTip.visible: hint.length > 0 && hovered
    ToolTip.delay: 450
    ToolTip.text: hint

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.down ? Theme.glassFillPressed
             : control.hovered ? (control.danger ? Theme.withAlpha(Theme.danger, 0.18) : Theme.glassFillHover)
             : control.flat ? "transparent" : Theme.glassFill
        border.width: control.flat && !control.hovered ? 0 : 1
        border.color: control.hovered ? Theme.withAlpha(control.danger ? Theme.danger : Theme.accent, 0.5) : Theme.glassStroke

        Behavior on color { ColorAnimation { duration: Motion.fast } }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.width: 2
            border.color: Theme.withAlpha(Theme.accent, 0.9)
            visible: control.visualFocus
        }
    }

    contentItem: Icon {
        glyph: control.glyph
        size: control.iconSize
        color: control.hovered ? (control.danger ? Theme.danger : Theme.accent) : Theme.textSecondary

        Behavior on color { ColorAnimation { duration: Motion.fast } }
    }
}
