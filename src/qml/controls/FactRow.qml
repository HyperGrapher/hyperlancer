import QtQuick

// One label/value line inside a SectionPanel. Long values wrap; a hairline separates rows.
// Click copies the value.
Item {
    id: row

    property string label: ""
    property string value: ""
    property color valueColor: Theme.textPrimary
    property bool mono: false
    property bool last: false
    property bool copyable: true
    property real labelWidth: Math.min(150, width * 0.38)

    signal copyRequested(string text)

    width: parent ? parent.width : 300
    implicitHeight: Math.max(32, valueText.implicitHeight + 14)

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: -6
        anchors.rightMargin: -6
        radius: 6
        color: hover.hovered && row.copyable && row.value !== "—" ? Theme.rowHover : "transparent"

        Behavior on color { ColorAnimation { duration: Motion.fast } }
    }

    Text {
        id: labelText

        width: row.labelWidth
        anchors.top: parent.top
        anchors.topMargin: 8
        text: row.label
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        elide: Text.ElideRight
    }

    Text {
        id: valueText

        anchors.left: labelText.right
        anchors.leftMargin: Theme.spacingM
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 7
        text: row.value
        color: row.valueColor
        font.family: row.mono ? "monospace" : Theme.fontFamily
        font.pixelSize: row.mono ? Theme.fontBody - 1 : Theme.fontBody
        wrapMode: Text.WrapAnywhere
        maximumLineCount: 6
        elide: Text.ElideRight
        textFormat: Text.PlainText
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.rowDivider
        visible: !row.last
    }

    HoverHandler {
        id: hover
    }

    TapHandler {
        enabled: row.copyable && row.value !== "—" && row.value.length > 0
        onTapped: row.copyRequested(row.value)
    }
}
