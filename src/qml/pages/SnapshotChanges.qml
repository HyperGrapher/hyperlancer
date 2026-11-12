import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hyperlancer

// What changed between one analysis and the one before it, grouped by section. Sections
// without changes are listed at the end so it is clear they were checked.
Item {
    id: panel

    property var diff: ({})
    property bool loading: false

    readonly property var groups: diff.groups || []
    readonly property string error: diff.error || ""

    signal backRequested

    function kindColor(kind) {
        return kind === "added" ? Theme.success : kind === "removed" ? Theme.danger : Theme.warning;
    }

    function kindGlyph(kind) {
        return kind === "added" ? Icons.plus : kind === "removed" ? Icons.close : Icons.arrowRight;
    }

    Item {
        id: bar

        width: parent.width
        height: 52

        IconButton {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingM
            anchors.verticalCenter: parent.verticalCenter
            glyph: Icons.arrowLeft
            hint: qsTr("Back to the list")
            onClicked: panel.backRequested()
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingM + 52
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingM
            anchors.verticalCenter: parent.verticalCenter
            text: panel.error.length > 0 || panel.loading ? qsTr("Changes")
                  : qsTr("Changes since %1").arg(Format.dateTime(panel.diff.beforeAt))
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontHeading
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.glassStroke
        }
    }

    Flickable {
        id: scroller

        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingL
        contentWidth: width
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        visible: !panel.loading && panel.error.length === 0 && panel.groups.length > 0

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: content

            width: scroller.width
            spacing: Theme.spacingL

            Text {
                text: qsTr("%n change(s) in %1 section(s)", "", panel.diff.count || 0).arg(panel.groups.length)
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
            }

            Repeater {
                model: panel.groups

                delegate: Column {
                    id: group

                    required property var modelData

                    width: content.width
                    spacing: 6

                    Text {
                        text: group.modelData.section.toUpperCase()
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1.1
                    }

                    Repeater {
                        model: group.modelData.changes

                        delegate: Item {
                            id: row

                            required property var modelData

                            width: group.width
                            height: Math.max(30, valueText.implicitHeight + 10)

                            Rectangle {
                                id: mark

                                anchors.verticalCenter: parent.verticalCenter
                                width: 22
                                height: 22
                                radius: 7
                                color: Qt.rgba(0.04, 0.02, 0.08, 0.6)
                                border.width: 1
                                border.color: Theme.withAlpha(panel.kindColor(row.modelData.kind), 0.6)

                                Icon {
                                    anchors.centerIn: parent
                                    glyph: panel.kindGlyph(row.modelData.kind)
                                    size: 12
                                    color: panel.kindColor(row.modelData.kind)
                                }
                            }

                            Text {
                                id: labelText

                                anchors.left: mark.right
                                anchors.leftMargin: Theme.spacingM
                                anchors.verticalCenter: parent.verticalCenter
                                width: 180
                                text: row.modelData.label
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                elide: Text.ElideRight
                            }

                            Text {
                                id: valueText

                                anchors.left: labelText.right
                                anchors.leftMargin: Theme.spacingM
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.modelData.kind === "added" ? row.modelData.after
                                      : row.modelData.kind === "removed" ? row.modelData.before
                                      : row.modelData.before + "  →  " + row.modelData.after
                                color: row.modelData.kind === "removed" ? Theme.textMuted : Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody + 1
                                font.strikeout: row.modelData.kind === "removed"
                                wrapMode: Text.Wrap
                                textFormat: Text.PlainText
                            }
                        }
                    }
                }
            }

            Text {
                width: parent.width
                visible: (panel.diff.unchangedSections || []).length > 0
                text: qsTr("No changes in: %1").arg((panel.diff.unchangedSections || []).join(", "))
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption + 1
                wrapMode: Text.WordWrap
            }
        }
    }

    EmptyState {
        anchors.centerIn: scroller
        width: scroller.width
        visible: !scroller.visible
        glyph: panel.error.length > 0 ? Icons.warningCircle : panel.loading ? Icons.hourglass : Icons.sealCheck
        title: panel.loading ? qsTr("Comparing…")
               : panel.error.length > 0 ? qsTr("Nothing to compare")
               : qsTr("Nothing changed")
        text: panel.error.length > 0 ? panel.error
              : panel.loading ? "" : qsTr("These two analyses found the same things.")
    }
}
