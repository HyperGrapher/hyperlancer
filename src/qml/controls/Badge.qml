import QtQuick

// Small pill for a status or category: tinted fill, matching border and text.
Rectangle {
    id: badge

    property string text: ""
    property string glyph: ""
    property color tint: Theme.accent
    property bool strong: false

    implicitWidth: row.implicitWidth + 16
    implicitHeight: 22
    radius: height / 2
    color: Theme.withAlpha(tint, strong ? 0.26 : 0.14)
    border.width: 1
    border.color: Theme.withAlpha(tint, strong ? 0.6 : 0.32)

    Behavior on color { ColorAnimation { duration: Motion.normal } }

    Row {
        id: row

        anchors.centerIn: parent
        spacing: 5

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            glyph: badge.glyph
            size: 12
            filled: true
            color: badge.tint
            visible: badge.glyph.length > 0
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: badge.text
            color: Theme.mix(badge.tint, Theme.textPrimary, 0.35)
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
            font.weight: Font.DemiBold
            font.letterSpacing: 0.2
        }
    }
}
