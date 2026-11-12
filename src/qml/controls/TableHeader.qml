import QtQuick
import QtQuick.Layouts

// Column captions above a list. Children are laid out with RowLayout, so a page gives each
// caption the same Layout widths as its row cells and the columns line up.
Item {
    id: header

    default property alias columns: layout.data
    property real horizontalPadding: Theme.spacingL

    implicitHeight: Theme.tableHeaderHeight

    RowLayout {
        id: layout

        anchors.fill: parent
        anchors.leftMargin: header.horizontalPadding
        anchors.rightMargin: header.horizontalPadding
        spacing: Theme.spacingM
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.glassStroke
    }

    component Caption: Text {
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: 10
        font.weight: Font.DemiBold
        font.letterSpacing: 1.1
        font.capitalization: Font.AllUppercase
        elide: Text.ElideRight
        Layout.alignment: Qt.AlignVCenter
    }

    // A caption that sorts the list when clicked; the active one shows its direction.
    component SortCaption: Caption {
        id: sortCaption

        property int sortKey: 0
        property int activeKey: 0
        property bool descending: false
        readonly property bool active: sortKey === activeKey

        signal sortRequested(int key)

        text: baseText + (active ? (descending ? "  ↓" : "  ↑") : "")
        color: active ? Theme.textPrimary : (sortHover.hovered ? Theme.textSecondary : Theme.textMuted)

        property string baseText: ""

        HoverHandler {
            id: sortHover

            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            onTapped: sortCaption.sortRequested(sortCaption.sortKey)
        }
    }
}
