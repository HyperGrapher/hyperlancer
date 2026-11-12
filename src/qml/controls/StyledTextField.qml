import QtQuick
import QtQuick.Controls

// Text input on a glass surface: optional leading icon, accent focus ring, and a clear
// button once there is text.
TextField {
    id: control

    property string glyph: ""
    property bool clearable: false

    implicitHeight: Theme.inputHeight
    implicitWidth: 260
    leftPadding: glyph.length > 0 ? 38 : 14
    rightPadding: clearable && text.length > 0 ? 34 : 14
    color: Theme.textPrimary
    placeholderTextColor: Theme.textMuted
    selectionColor: Theme.withAlpha(Theme.accent, 0.45)
    selectedTextColor: Theme.textPrimary
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody + 1
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true
    hoverEnabled: true
    opacity: enabled ? 1 : 0.5

    background: Rectangle {
        radius: Theme.radiusControl
        color: control.activeFocus ? Qt.rgba(0.02, 0, 0.06, 0.42) : control.hovered ? Theme.glassFillHover : Theme.glassTint
        border.width: control.activeFocus ? 1.5 : 1
        border.color: control.activeFocus ? Theme.withAlpha(Theme.accent, 0.85) : Theme.glassStroke

        Behavior on color { ColorAnimation { duration: Motion.fast } }
        Behavior on border.color { ColorAnimation { duration: Motion.fast } }

        Icon {
            x: 13
            anchors.verticalCenter: parent.verticalCenter
            glyph: control.glyph
            size: 16
            visible: control.glyph.length > 0
            color: control.activeFocus ? Theme.accent : Theme.textMuted

            Behavior on color { ColorAnimation { duration: Motion.fast } }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.width: 3
            border.color: Theme.withAlpha(Theme.accent, 0.16)
            opacity: control.activeFocus ? 1 : 0

            Behavior on opacity { NumberAnimation { duration: Motion.normal } }
        }
    }

    IconButton {
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 28
        height: 28
        glyph: Icons.close
        iconSize: 13
        flat: true
        visible: control.clearable && control.text.length > 0
        onClicked: {
            control.clear();
            control.forceActiveFocus();
        }
    }
}
