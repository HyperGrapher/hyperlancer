import QtQuick

// Stack of short notices in the bottom-right corner. Each slides up, stays a few seconds
// (errors longer) and fades out; hovering keeps it open.
Item {
    id: toasts

    function show(message, isError) {
        if (!message)
            return;
        model.append({ message: String(message), isError: !!isError });
        if (model.count > 4)
            model.remove(0);
    }

    width: 380
    height: column.height

    ListModel {
        id: model
    }

    Column {
        id: column

        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: parent.width
        spacing: Theme.spacingS

        move: Transition {
            NumberAnimation { properties: "y"; duration: Motion.normal; easing.type: Easing.OutCubic }
        }

        Repeater {
            model: model

            delegate: Rectangle {
                id: toast

                required property int index
                required property string message
                required property bool isError

                width: column.width
                height: text.implicitHeight + 26
                radius: Theme.radiusControl
                color: Qt.rgba(0.04, 0.02, 0.08, 0.86)
                border.width: 1
                border.color: Theme.withAlpha(isError ? Theme.danger : Theme.accent, 0.55)
                opacity: 0
                transform: Translate { id: shift; y: 14 }

                Component.onCompleted: enter.start()

                ParallelAnimation {
                    id: enter

                    NumberAnimation { target: toast; property: "opacity"; to: 1; duration: Motion.normal; easing.type: Easing.OutCubic }
                    NumberAnimation { target: shift; property: "y"; to: 0; duration: Motion.normal + 80; easing.type: Easing.OutBack }
                }

                SequentialAnimation {
                    id: leave

                    NumberAnimation { target: toast; property: "opacity"; to: 0; duration: Motion.normal }
                    ScriptAction { script: model.remove(toast.index) }
                }

                Timer {
                    interval: toast.isError ? 7000 : 4000
                    running: !hover.hovered
                    onTriggered: leave.start()
                }

                Rectangle {
                    width: 3
                    radius: 1.5
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.margins: 9
                    color: toast.isError ? Theme.danger : Theme.accent
                }

                Icon {
                    id: icon

                    x: 20
                    y: 13
                    glyph: toast.isError ? Icons.warningCircle : Icons.checkCircle
                    filled: true
                    size: 16
                    color: toast.isError ? Theme.danger : Theme.accent
                }

                Text {
                    id: text

                    anchors.left: icon.right
                    anchors.leftMargin: 10
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    y: 13
                    text: toast.message
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap
                }

                HoverHandler {
                    id: hover
                }

                TapHandler {
                    onTapped: leave.start()
                }
            }
        }
    }
}
