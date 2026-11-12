import QtQuick

// One headline number on a glass card: icon badge and label on top, the animated value at
// the bottom, a short note on the right.
GlassCard {
    id: card

    property string glyph: Icons.chartBar
    property string label: ""
    property string note: ""
    property color noteColor: Theme.accent
    property real value: 0
    property int decimals: 0

    function replay() {
        number.replay();
    }

    implicitHeight: 112

    Rectangle {
        id: badge

        x: Theme.spacingL + 2
        y: Theme.spacingL + 2
        width: 32
        height: 32
        radius: 10
        color: Theme.withAlpha(Theme.accent, 0.16)

        Icon {
            anchors.centerIn: parent
            glyph: card.glyph
            size: 17
            color: Theme.accent
        }
    }

    Text {
        anchors.left: badge.right
        anchors.leftMargin: Theme.spacingM
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingL
        anchors.verticalCenter: badge.verticalCenter
        text: card.label
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        font.weight: Font.Medium
        elide: Text.ElideRight
    }

    AnimatedNumber {
        id: number

        value: card.value
        decimals: card.decimals
        x: Theme.spacingL + 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingL - 2
        font.pixelSize: 30
        font.weight: Font.Bold
        font.letterSpacing: -0.8
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingL + 2
        anchors.left: number.right
        anchors.leftMargin: Theme.spacingS
        anchors.baseline: number.baseline
        horizontalAlignment: Text.AlignRight
        text: card.note
        color: card.noteColor
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontCaption + 1
        font.weight: Font.Medium
        elide: Text.ElideLeft
    }
}
