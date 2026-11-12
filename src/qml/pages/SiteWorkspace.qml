import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hyperlancer

// The user's own record of a site: lead status, category, tags, to-dos and notes. Every
// change saves at once and the detail reloads.
Flickable {
    id: workspace

    required property AppContext app

    readonly property SiteDetail detail: app.siteDetail
    readonly property var organizer: detail.organizer
    readonly property var tags: organizer.tags || []
    readonly property var tagNames: tags.map(tag => tag.name)
    readonly property var todos: organizer.todos || []
    readonly property var notes: organizer.notes || []
    readonly property string todayIso: Qt.formatDate(new Date(), "yyyy-MM-dd")

    contentWidth: width
    contentHeight: column.implicitHeight + Theme.spacingL
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    Column {
        id: column

        width: workspace.width
        spacing: Theme.spacingM

        // ---- status, category, tags ----
        Section {
            title: qsTr("Status")

            SegmentedControl {
                options: workspace.detail.statuses.map(status => Format.leadStatusLabel(status))
                currentIndex: Math.max(0, workspace.detail.statuses.indexOf(workspace.organizer.status))
                onActivated: index => workspace.detail.setStatus(workspace.detail.statuses[index])
            }
        }

        Section {
            title: qsTr("Category")

            Flow {
                width: parent.width
                spacing: Theme.spacingS

                Repeater {
                    model: workspace.organizer.categories || []

                    delegate: Chip {
                        required property string modelData

                        text: modelData
                        selected: modelData === workspace.organizer.category
                        onClicked: workspace.detail.setCategory(selected ? "" : modelData)
                    }
                }

                StyledTextField {
                    id: categoryField

                    implicitWidth: 200
                    implicitHeight: 30
                    placeholderText: qsTr("New category…")
                    onAccepted: {
                        workspace.detail.setCategory(text);
                        clear();
                    }
                }
            }
        }

        Section {
            title: qsTr("Tags")

            Flow {
                width: parent.width
                spacing: Theme.spacingS

                Repeater {
                    model: workspace.tags

                    delegate: Chip {
                        required property var modelData

                        text: modelData.name
                        selected: true
                        removable: true
                        onRemoveRequested: workspace.detail.removeTag(modelData.id)
                    }
                }

                StyledTextField {
                    implicitWidth: 200
                    implicitHeight: 30
                    placeholderText: qsTr("Add a tag…")
                    onAccepted: {
                        workspace.detail.addTag(text);
                        clear();
                    }
                }
            }

            // Tags used on other sites, one click to reuse.
            Flow {
                width: parent.width
                spacing: Theme.spacingS
                visible: suggestions.count > 0

                Repeater {
                    id: suggestions

                    model: (workspace.organizer.allTags || []).filter(name => workspace.tagNames.indexOf(name) < 0)

                    delegate: Chip {
                        required property string modelData

                        text: "+ " + modelData
                        onClicked: workspace.detail.addTag(modelData)
                    }
                }
            }
        }

        // ---- to-dos ----
        Section {
            title: qsTr("To-dos")

            RowLayout {
                width: parent.width
                spacing: Theme.spacingS

                StyledTextField {
                    id: todoField

                    Layout.fillWidth: true
                    implicitHeight: 34
                    placeholderText: qsTr("What needs doing?")
                    onAccepted: workspace.addTodo()
                }

                StyledTextField {
                    id: dueField

                    implicitWidth: 130
                    implicitHeight: 34
                    placeholderText: qsTr("YYYY-MM-DD")
                    validator: RegularExpressionValidator { regularExpression: /^(\d{4}-\d{2}-\d{2})?$/ }
                    onAccepted: workspace.addTodo()
                }

                SecondaryButton {
                    implicitHeight: 34
                    text: qsTr("Add")
                    glyph: Icons.plus
                    enabled: todoField.text.trim().length > 0
                    onClicked: workspace.addTodo()
                }
            }

            Repeater {
                model: workspace.todos

                delegate: Item {
                    id: todoRow

                    required property var modelData
                    readonly property bool overdue: !modelData.done && modelData.dueOn.length > 0 && modelData.dueOn < workspace.todayIso

                    width: parent.width
                    height: 38

                    Rectangle {
                        id: box

                        x: 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: 20
                        height: 20
                        radius: 6
                        color: todoRow.modelData.done ? Theme.withAlpha(Theme.success, 0.25) : "transparent"
                        border.width: 1.5
                        border.color: todoRow.modelData.done ? Theme.success : Theme.textMuted

                        Icon {
                            anchors.centerIn: parent
                            glyph: Icons.check
                            size: 12
                            color: Theme.success
                            visible: todoRow.modelData.done
                        }

                        TapHandler {
                            onTapped: workspace.detail.setTodoDone(todoRow.modelData.id, !todoRow.modelData.done)
                        }
                    }

                    Text {
                        anchors.left: box.right
                        anchors.leftMargin: Theme.spacingM
                        anchors.right: dueText.left
                        anchors.rightMargin: Theme.spacingM
                        anchors.verticalCenter: parent.verticalCenter
                        text: todoRow.modelData.title
                        color: todoRow.modelData.done ? Theme.textMuted : Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody + 1
                        font.strikeout: todoRow.modelData.done
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }

                    Text {
                        id: dueText

                        anchors.right: removeTodo.left
                        anchors.rightMargin: Theme.spacingS
                        anchors.verticalCenter: parent.verticalCenter
                        text: todoRow.modelData.dueOn
                        color: todoRow.overdue ? Theme.danger : Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption + 1
                    }

                    IconButton {
                        id: removeTodo

                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Icons.trash
                        flat: true
                        danger: true
                        hint: qsTr("Delete to-do")
                        onClicked: workspace.detail.deleteTodo(todoRow.modelData.id)
                    }
                }
            }

            Text {
                visible: workspace.todos.length === 0
                text: qsTr("No to-dos yet.")
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
            }
        }

        // ---- notes ----
        Section {
            title: qsTr("Notes")

            Rectangle {
                width: parent.width
                height: 84
                radius: Theme.radiusControl
                color: Theme.glassTint
                border.width: 1
                border.color: noteField.activeFocus ? Theme.withAlpha(Theme.accent, 0.9) : Theme.glassStroke

                TextArea {
                    id: noteField

                    anchors.fill: parent
                    placeholderText: qsTr("Write a note… (Ctrl+Enter to save)")
                    placeholderTextColor: Theme.textMuted
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody + 1
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    background: null
                    Keys.onPressed: event => {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                            workspace.addNote();
                            event.accepted = true;
                        }
                    }
                }
            }

            SecondaryButton {
                implicitHeight: 34
                text: qsTr("Save note")
                glyph: Icons.plus
                enabled: noteField.text.trim().length > 0
                onClicked: workspace.addNote()
            }

            Repeater {
                model: workspace.notes

                delegate: Rectangle {
                    id: noteCard

                    required property var modelData

                    width: parent.width
                    height: noteBody.implicitHeight + noteDate.implicitHeight + Theme.spacingM * 2 + 4
                    radius: Theme.radiusSmall
                    color: Theme.glassFill

                    Text {
                        id: noteDate

                        x: Theme.spacingM
                        y: Theme.spacingM
                        text: Format.dateTime(noteCard.modelData.createdAt)
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                    }

                    Text {
                        id: noteBody

                        x: Theme.spacingM
                        anchors.top: noteDate.bottom
                        anchors.topMargin: 4
                        width: parent.width - Theme.spacingM * 2 - 34
                        text: noteCard.modelData.body
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody + 1
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                    }

                    IconButton {
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.top: parent.top
                        anchors.topMargin: 4
                        glyph: Icons.trash
                        flat: true
                        danger: true
                        hint: qsTr("Delete note")
                        onClicked: workspace.detail.deleteNote(noteCard.modelData.id)
                    }
                }
            }
        }
    }

    function addTodo() {
        if (todoField.text.trim().length === 0)
            return;
        detail.addTodo(todoField.text, dueField.text);
        todoField.clear();
        dueField.clear();
    }

    function addNote() {
        if (noteField.text.trim().length === 0)
            return;
        detail.addNote(noteField.text);
        noteField.clear();
    }

    // A titled glass block; children stack inside.
    component Section: GlassPanel {
        id: section

        property string title: ""
        default property alias content: body.data

        width: column.width
        height: body.implicitHeight + titleText.height + Theme.spacingL * 2 + Theme.spacingS
        radius: Theme.radiusCard

        Text {
            id: titleText

            x: Theme.spacingL
            y: Theme.spacingL
            text: section.title
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontHeading
            font.weight: Font.DemiBold
        }

        Column {
            id: body

            x: Theme.spacingL
            anchors.top: titleText.bottom
            anchors.topMargin: Theme.spacingS
            width: parent.width - Theme.spacingL * 2
            spacing: Theme.spacingS
        }
    }

    // A pill that can be selected, clicked, and optionally removed.
    component Chip: Rectangle {
        id: chip

        property string text: ""
        property bool selected: false
        property bool removable: false

        signal clicked
        signal removeRequested

        implicitHeight: 30
        implicitWidth: label.implicitWidth + (removable ? 44 : 26)
        radius: height / 2
        color: selected ? Theme.withAlpha(Theme.accent, 0.24) : chipHover.hovered ? Theme.glassFillHover : Theme.glassFill
        border.width: 1
        border.color: selected ? Theme.withAlpha(Theme.accent, 0.55) : Theme.glassStroke

        Text {
            id: label

            x: 13
            anchors.verticalCenter: parent.verticalCenter
            text: chip.text
            color: chip.selected ? Theme.textPrimary : Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
        }

        Icon {
            id: removeIcon

            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            glyph: Icons.close
            size: 12
            color: removeHover.hovered ? Theme.danger : Theme.textMuted
            visible: chip.removable

            HoverHandler {
                id: removeHover

                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: chip.removeRequested()
            }
        }

        HoverHandler {
            id: chipHover

            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            onTapped: chip.clicked()
        }
    }
}
