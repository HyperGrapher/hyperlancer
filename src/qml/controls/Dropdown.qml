import QtQuick
import QtQuick.Controls

// A button that opens a list to pick from. Options are { value, label, tint? } objects.
// "searchable" adds a filter field; "allowCreate" offers the typed text as a new value
// (signal created). Picking an option emits activated(value).
Item {
    id: control

    property var options: []
    property var currentValue: ""
    property string placeholder: ""
    property bool searchable: false
    property bool allowCreate: false
    property string createLabel: qsTr("Create “%1”")
    property bool compact: false
    property real popupWidth: Math.max(width, 200)
    // The value whose option is shown on the button; a missing one shows the placeholder.
    readonly property var currentOption: options.find(option => option.value === currentValue)

    signal activated(var value)
    signal created(string text)

    function open() {
        popup.open();
    }

    implicitWidth: 160
    implicitHeight: compact ? 28 : Theme.inputHeight
    opacity: enabled ? 1 : 0.5

    Rectangle {
        anchors.fill: parent
        radius: control.compact ? Theme.radiusSmall : Theme.radiusControl
        color: popup.opened || hover.hovered ? Theme.glassFillHover : control.compact ? "transparent" : Theme.glassTint
        border.width: control.compact && !hover.hovered && !popup.opened ? 0 : 1
        border.color: popup.opened ? Theme.withAlpha(Theme.accent, 0.8) : Theme.glassStroke

        Behavior on color { ColorAnimation { duration: Motion.fast } }
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: control.compact ? 8 : 12
        anchors.right: caret.left
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        spacing: 7

        Rectangle {
            id: tintDot

            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: control.currentOption && control.currentOption.tint ? control.currentOption.tint : "transparent"
            visible: control.currentOption !== undefined && control.currentOption.tint !== undefined
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - (tintDot.visible ? 15 : 0)
            text: control.currentOption ? control.currentOption.label : control.placeholder
            color: control.currentOption ? Theme.textPrimary : Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: control.compact ? Theme.fontBody : Theme.fontBody + 1
            elide: Text.ElideRight
        }
    }

    Icon {
        id: caret

        anchors.right: parent.right
        anchors.rightMargin: control.compact ? 6 : 10
        anchors.verticalCenter: parent.verticalCenter
        glyph: Icons.caretDown
        size: 12
        color: Theme.textMuted
    }

    HoverHandler {
        id: hover

        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: popup.opened ? popup.close() : popup.open()
    }

    Popup {
        id: popup

        readonly property string query: searchField.text.trim().toLowerCase()
        readonly property var shown: control.options.filter(option => query.length === 0 || option.label.toLowerCase().indexOf(query) >= 0)
        readonly property bool canCreate: control.allowCreate && query.length > 0 &&
                                          !control.options.some(option => option.label.toLowerCase() === query)

        y: control.height + 4
        width: control.popupWidth
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: {
            searchField.clear();
            if (control.searchable || control.allowCreate)
                searchField.forceActiveFocus();
        }

        background: Rectangle {
            radius: Theme.radiusControl
            color: Qt.rgba(0.07, 0.05, 0.12, 0.97)
            border.width: 1
            border.color: Theme.glassStroke
        }

        function pick(value) {
            control.activated(value);
            popup.close();
        }

        function create(text) {
            control.created(text.trim());
            popup.close();
        }

        contentItem: Column {
            spacing: 4

            StyledTextField {
                id: searchField

                width: parent.width
                implicitHeight: 32
                visible: control.searchable || control.allowCreate
                height: visible ? implicitHeight : 0
                glyph: Icons.search
                placeholderText: qsTr("Type to filter…")
                onAccepted: {
                    if (popup.shown.length > 0)
                        popup.pick(popup.shown[0].value);
                    else if (popup.canCreate)
                        popup.create(text);
                }
            }

            ListView {
                id: list

                width: parent.width
                height: Math.min(contentHeight, 260)
                clip: true
                model: popup.shown
                boundsBehavior: Flickable.StopAtBounds

                delegate: Item {
                    id: option

                    required property var modelData

                    width: list.width
                    height: 34

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusSmall
                        color: optionHover.hovered ? Theme.rowHover : "transparent"
                    }

                    Rectangle {
                        id: dot

                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 8
                        radius: 4
                        color: option.modelData.tint !== undefined ? option.modelData.tint : "transparent"
                        visible: option.modelData.tint !== undefined
                    }

                    Text {
                        anchors.left: dot.visible ? dot.right : parent.left
                        anchors.leftMargin: dot.visible ? 8 : 10
                        anchors.right: check.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: option.modelData.label
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody + 1
                        elide: Text.ElideRight
                    }

                    Icon {
                        id: check

                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Icons.check
                        size: 13
                        color: Theme.accent
                        visible: option.modelData.value === control.currentValue
                    }

                    HoverHandler {
                        id: optionHover

                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        onTapped: popup.pick(option.modelData.value)
                    }
                }
            }

            Item {
                width: parent.width
                height: popup.canCreate ? 34 : 0
                visible: popup.canCreate

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSmall
                    color: createHover.hovered ? Theme.rowHover : "transparent"
                }

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Icons.plus
                        size: 13
                        color: Theme.accent
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: control.createLabel.arg(searchField.text.trim())
                        color: Theme.accent
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody + 1
                    }
                }

                HoverHandler {
                    id: createHover

                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    onTapped: popup.create(searchField.text)
                }
            }

            Text {
                width: parent.width
                height: popup.shown.length === 0 && !popup.canCreate ? 30 : 0
                visible: height > 0
                text: qsTr("Nothing to show")
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
