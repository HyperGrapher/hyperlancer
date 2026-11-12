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
    property int expandedNote: -1
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

        // ---- status, category, deadline ----
        Section {
            title: qsTr("Details")

            GridLayout {
                width: parent.width
                columns: 2
                columnSpacing: Theme.spacingL
                rowSpacing: Theme.spacingS

                FieldLabel { text: qsTr("Status") }
                Dropdown {
                    Layout.preferredWidth: 240
                    options: Format.leadStatusOptions
                    currentValue: workspace.organizer.status || "none"
                    onActivated: value => workspace.detail.setStatus(value)
                }

                FieldLabel { text: qsTr("Category") }
                Dropdown {
                    Layout.preferredWidth: 240
                    options: [{ value: "", label: qsTr("No category") }]
                        .concat((workspace.organizer.categories || []).map(name => ({ value: name, label: name })))
                    currentValue: workspace.organizer.category || ""
                    placeholder: qsTr("No category")
                    searchable: true
                    allowCreate: true
                    createLabel: qsTr("New category “%1”")
                    onActivated: value => workspace.detail.setCategory(value)
                    onCreated: text => workspace.detail.setCategory(text)
                }

                FieldLabel { text: qsTr("Deadline") }
                DateField {
                    Layout.preferredWidth: 240
                    value: workspace.organizer.deadlineOn || ""
                    placeholder: qsTr("No deadline")
                    onPicked: value => workspace.detail.setDeadline(value)
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

                // Click to see the tags used elsewhere; typing narrows the list or makes a new tag.
                Dropdown {
                    width: 170
                    implicitHeight: 30
                    compact: true
                    placeholder: qsTr("Add a tag…")
                    options: (workspace.organizer.allTags || [])
                        .filter(name => workspace.tagNames.indexOf(name) < 0)
                        .map(name => ({ value: name, label: name }))
                    searchable: true
                    allowCreate: true
                    createLabel: qsTr("New tag “%1”")
                    onActivated: value => workspace.detail.addTag(value)
                    onCreated: text => workspace.detail.addTag(text)
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

                DateField {
                    id: dueField

                    implicitWidth: 150
                    implicitHeight: 34
                    placeholder: qsTr("Due date")
                    onPicked: value => dueField.value = value
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
                        text: todoRow.modelData.dueOn.length > 0 ? Format.shortDate(todoRow.modelData.dueOn) : ""
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

            StyledTextField {
                id: noteTitle

                width: parent.width
                implicitHeight: 34
                placeholderText: qsTr("Title")
            }

            Rectangle {
                width: parent.width
                height: 96
                radius: Theme.radiusControl
                color: Theme.glassTint
                border.width: 1
                border.color: noteField.activeFocus ? Theme.withAlpha(Theme.accent, 0.9) : Theme.glassStroke

                TextArea {
                    id: noteField

                    anchors.fill: parent
                    placeholderText: qsTr("Write in Markdown… (Ctrl+Enter to save)")
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

            // One accordion row per note: the title, and the Markdown body when opened.
            Repeater {
                model: workspace.notes

                delegate: Rectangle {
                    id: noteCard

                    required property var modelData
                    readonly property bool expanded: workspace.expandedNote === modelData.id

                    width: parent.width
                    height: noteHeader.height + (expanded ? noteBody.implicitHeight + Theme.spacingM * 2 : 0)
                    radius: Theme.radiusSmall
                    color: Theme.glassFill
                    clip: true

                    Behavior on height { NumberAnimation { duration: Motion.normal; easing.type: Easing.OutCubic } }

                    Item {
                        id: noteHeader

                        width: parent.width
                        height: 44

                        Icon {
                            id: chevron

                            x: Theme.spacingM
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: noteCard.expanded ? Icons.caretDown : Icons.caretRight
                            size: 14
                            color: Theme.textMuted
                        }

                        Text {
                            anchors.left: chevron.right
                            anchors.leftMargin: Theme.spacingS
                            anchors.right: noteDate.left
                            anchors.rightMargin: Theme.spacingM
                            anchors.verticalCenter: parent.verticalCenter
                            text: noteCard.modelData.title.length > 0 ? noteCard.modelData.title : qsTr("Untitled note")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody + 1
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }

                        Text {
                            id: noteDate

                            anchors.right: removeNote.left
                            anchors.rightMargin: Theme.spacingS
                            anchors.verticalCenter: parent.verticalCenter
                            text: Format.shortDate(noteCard.modelData.createdAt)
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontCaption + 1
                        }

                        IconButton {
                            id: removeNote

                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: Icons.trash
                            flat: true
                            danger: true
                            hint: qsTr("Delete note")
                            onClicked: workspace.detail.deleteNote(noteCard.modelData.id)
                        }

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }

                        TapHandler {
                            // The delete button handles its own taps.
                            onTapped: eventPoint => {
                                if (!removeNote.contains(removeNote.mapFromItem(noteHeader, eventPoint.position)))
                                    workspace.expandedNote = noteCard.expanded ? -1 : noteCard.modelData.id;
                            }
                        }
                    }

                    Text {
                        id: noteBody

                        x: Theme.spacingM
                        anchors.top: noteHeader.bottom
                        width: parent.width - Theme.spacingM * 2
                        text: noteCard.modelData.body
                        textFormat: Text.MarkdownText
                        color: Theme.textPrimary
                        linkColor: Theme.accent
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody + 1
                        wrapMode: Text.Wrap
                        opacity: noteCard.expanded ? 1 : 0
                        onLinkActivated: link => workspace.app.openExternal(link)

                        Behavior on opacity { NumberAnimation { duration: Motion.normal } }
                    }
                }
            }
        }
    }

    function addTodo() {
        if (todoField.text.trim().length === 0)
            return;
        detail.addTodo(todoField.text, dueField.value);
        todoField.clear();
        dueField.value = "";
    }

    function addNote() {
        if (noteField.text.trim().length === 0)
            return;
        detail.addNote(noteTitle.text, noteField.text);
        noteTitle.clear();
        noteField.clear();
    }

    component FieldLabel: Text {
        Layout.preferredWidth: 90
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody + 1
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
