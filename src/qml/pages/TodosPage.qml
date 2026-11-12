import QtQuick
import QtQuick.Controls
import Hyperlancer

// Every to-do of every website in one list. Open ones come soonest first, grouped by when
// they are due; tick one off from here, or click its website to see it in context.
PageBase {
    id: page

    required property AppContext app

    readonly property TodoList todos: app.todos
    readonly property string todayIso: Qt.formatDate(new Date(), "yyyy-MM-dd")

    signal siteOpened(var siteId)

    // Rows for the list: a group heading, then its to-dos.
    readonly property var rows: {
        const items = todos.items;
        if (todos.showDone)
            return items.map(item => ({ kind: "todo", item: item }));
        const groups = [
            { name: qsTr("Overdue"), test: days => days !== null && days < 0 },
            { name: qsTr("Today"), test: days => days === 0 },
            { name: qsTr("Next 7 days"), test: days => days !== null && days > 0 && days <= 7 },
            { name: qsTr("Later"), test: days => days !== null && days > 7 },
            { name: qsTr("No date"), test: days => days === null }
        ];
        const result = [];
        for (const group of groups) {
            const members = items.filter(item => group.test(item.dueOn.length > 0 ? Format.daysUntil(item.dueOn) : null));
            if (members.length === 0)
                continue;
            result.push({ kind: "heading", name: group.name, count: members.length });
            for (const item of members)
                result.push({ kind: "todo", item: item });
        }
        return result;
    }

    onEntered: todos.refresh()

    PageHeader {
        id: header

        width: parent.width
        title: qsTr("To-dos")
        subtitle: page.todos.openCount === 0 ? qsTr("Nothing open. Add to-dos on a website's Workspace tab.")
                  : qsTr("%n open to-do(s) across all websites.", "", page.todos.openCount)
        opacity: page.reveal(0)
        transform: Translate { y: page.revealShift(0) }

        SegmentedControl {
            implicitHeight: Theme.inputHeight
            options: [qsTr("Open"), qsTr("Completed")]
            currentIndex: page.todos.showDone ? 1 : 0
            onActivated: index => page.todos.showDone = index === 1
        }
    }

    GlassPanel {
        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingXl
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        radius: Theme.radiusCard
        clip: true
        opacity: page.reveal(1)
        transform: Translate { y: page.revealShift(1) }

        ListView {
            id: list

            anchors.fill: parent
            anchors.margins: 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: page.rows

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                id: row

                required property var modelData
                readonly property bool isHeading: modelData.kind === "heading"
                readonly property var item: modelData.item
                readonly property var days: !isHeading && item.dueOn.length > 0 ? Format.daysUntil(item.dueOn) : null

                width: list.width
                height: isHeading ? 40 : 46

                // ---- group heading ----
                Text {
                    visible: row.isHeading
                    x: Theme.spacingL - 4
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    text: row.isHeading ? row.modelData.name.toUpperCase() + "  ·  " + row.modelData.count : ""
                    color: row.modelData.name === qsTr("Overdue") ? Theme.danger : Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.1
                }

                // ---- to-do ----
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: Theme.radiusSmall
                    color: rowHover.containsMouse ? Theme.rowHover : "transparent"
                    visible: !row.isHeading
                }

                Rectangle {
                    id: box

                    visible: !row.isHeading
                    x: Theme.spacingL - 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    radius: 6
                    color: !row.isHeading && row.item.done ? Theme.withAlpha(Theme.success, 0.25) : "transparent"
                    border.width: 1.5
                    border.color: !row.isHeading && row.item.done ? Theme.success : Theme.textMuted

                    Icon {
                        anchors.centerIn: parent
                        glyph: Icons.check
                        size: 12
                        color: Theme.success
                        visible: !row.isHeading && row.item.done
                    }

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -6
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.todos.setDone(row.item.id, !row.item.done)
                    }
                }

                Text {
                    visible: !row.isHeading
                    anchors.left: box.right
                    anchors.leftMargin: Theme.spacingM
                    anchors.right: siteText.left
                    anchors.rightMargin: Theme.spacingM
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.isHeading ? "" : row.item.title
                    color: !row.isHeading && row.item.done ? Theme.textMuted : Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody + 1
                    font.strikeout: !row.isHeading && row.item.done
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }

                Text {
                    id: siteText

                    visible: !row.isHeading
                    anchors.right: dueText.left
                    anchors.rightMargin: Theme.spacingL
                    anchors.verticalCenter: parent.verticalCenter
                    width: 160
                    horizontalAlignment: Text.AlignRight
                    text: row.isHeading ? "" : row.item.siteKey
                    color: siteHover.containsMouse ? Theme.accent : Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    elide: Text.ElideRight

                    MouseArea {
                        id: siteHover

                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.siteOpened(row.item.siteId)
                    }
                }

                Text {
                    id: dueText

                    visible: !row.isHeading
                    anchors.right: removeButton.left
                    anchors.rightMargin: Theme.spacingS
                    anchors.verticalCenter: parent.verticalCenter
                    width: 96
                    horizontalAlignment: Text.AlignRight
                    text: row.isHeading || row.item.dueOn.length === 0 ? "" : Format.shortDate(row.item.dueOn)
                    color: row.item && !row.item.done ? Format.dueColor(row.days) : Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption + 1
                }

                IconButton {
                    id: removeButton

                    visible: !row.isHeading
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingS
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Icons.trash
                    flat: true
                    danger: true
                    hint: qsTr("Delete to-do")
                    onClicked: page.todos.remove(row.item.id)
                }

                // Hover for the whole row; the controls above take their own clicks.
                MouseArea {
                    id: rowHover

                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                }
            }
        }

        EmptyState {
            anchors.centerIn: parent
            width: parent.width
            visible: page.rows.length === 0
            glyph: Icons.checkSquare
            title: page.todos.showDone ? qsTr("Nothing completed yet") : qsTr("All clear")
            text: page.todos.showDone ? qsTr("Completed to-dos appear here.") : qsTr("Open a website's Workspace tab to add a to-do with a due date.")
        }
    }
}
