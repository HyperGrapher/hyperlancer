import QtQuick

// Centered message for an empty list or a section that needs setup: icon in a soft circle,
// a title, one or two lines of help and optional actions.
Item {
    id: empty

    property string glyph: Icons.tray
    property string title: ""
    property string text: ""
    default property alias actions: actionRow.data

    implicitHeight: column.implicitHeight + 40
    implicitWidth: 360

    Column {
        id: column

        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 420)
        spacing: Theme.spacingM

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 64
            height: 64
            radius: 32
            color: Theme.withAlpha(Theme.accent, 0.12)
            border.width: 1
            border.color: Theme.withAlpha(Theme.accent, 0.3)

            Icon {
                anchors.centerIn: parent
                glyph: empty.glyph
                size: 28
                color: Theme.accent
            }
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: empty.title
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontHeading + 1
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: empty.text
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WordWrap
            lineHeight: 1.15
            visible: text.length > 0
        }

        Row {
            id: actionRow

            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.spacingS
            topPadding: Theme.spacingXs
        }
    }
}
