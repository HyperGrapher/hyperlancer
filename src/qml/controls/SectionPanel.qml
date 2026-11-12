import QtQuick

// Titled glass group for one block of facts (a profile section, a settings group). The
// header carries an icon, the title, an optional status badge and optional actions.
GlassPanel {
    id: panel

    property string title: ""
    property string glyph: ""
    property string status: ""          // "", "ok", "failed", "partial", "skipped"
    property string statusText: ""
    property real contentSpacing: 0
    default property alias content: body.data
    property alias actions: actionRow.data

    radius: Theme.radiusCard
    implicitHeight: body.implicitHeight + headerRow.height + Theme.spacingL * 2 + Theme.spacingS

    Item {
        id: headerRow

        x: Theme.spacingL + 2
        y: Theme.spacingL
        width: parent.width - 2 * (Theme.spacingL + 2)
        height: 26

        Icon {
            id: headerIcon

            anchors.verticalCenter: parent.verticalCenter
            glyph: panel.glyph
            size: 16
            color: Theme.accent
            visible: panel.glyph.length > 0
        }

        Text {
            anchors.left: headerIcon.visible ? headerIcon.right : parent.left
            anchors.leftMargin: headerIcon.visible ? Theme.spacingS + 2 : 0
            anchors.right: statusBadge.visible ? statusBadge.left : actionRow.left
            anchors.rightMargin: Theme.spacingS
            anchors.verticalCenter: parent.verticalCenter
            text: panel.title
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontHeading
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Badge {
            id: statusBadge

            anchors.right: actionRow.left
            anchors.rightMargin: actionRow.width > 0 ? Theme.spacingS : 0
            anchors.verticalCenter: parent.verticalCenter
            visible: panel.statusText.length > 0
            text: panel.statusText
            tint: Theme.statusColor(panel.status)
        }

        Row {
            id: actionRow

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingXs
        }
    }

    Column {
        id: body

        x: Theme.spacingL + 2
        y: headerRow.y + headerRow.height + Theme.spacingS
        width: parent.width - 2 * (Theme.spacingL + 2)
        spacing: panel.contentSpacing
    }
}
