import QtQuick
import QtQuick.Controls

// Picks a date from a calendar. "value" is YYYY-MM-DD or empty; the dates cannot be typed.
Item {
    id: control

    property string value: ""
    property string placeholder: qsTr("Pick a date")
    property bool compact: false
    readonly property date selected: value.length > 0 ? new Date(value + "T12:00:00") : new Date()

    signal picked(string value) // an ISO date, or "" when cleared

    function open() {
        popup.open();
    }

    function iso(date) {
        return Qt.formatDate(date, "yyyy-MM-dd");
    }

    implicitWidth: 150
    implicitHeight: compact ? 28 : Theme.inputHeight

    Rectangle {
        anchors.fill: parent
        radius: control.compact ? Theme.radiusSmall : Theme.radiusControl
        color: popup.opened || hover.hovered ? Theme.glassFillHover : Theme.glassTint
        border.width: 1
        border.color: popup.opened ? Theme.withAlpha(Theme.accent, 0.8) : Theme.glassStroke
    }

    Icon {
        id: icon

        x: 10
        anchors.verticalCenter: parent.verticalCenter
        glyph: Icons.calendar
        size: 14
        color: Theme.textMuted
    }

    Text {
        anchors.left: icon.right
        anchors.leftMargin: 8
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: control.value.length > 0 ? Qt.formatDate(control.selected, Qt.locale().dateFormat(Locale.ShortFormat)) : control.placeholder
        color: control.value.length > 0 ? Theme.textPrimary : Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody + 1
        elide: Text.ElideRight
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

        property int shownYear: control.selected.getFullYear()
        property int shownMonth: control.selected.getMonth()

        function step(delta) {
            const next = new Date(shownYear, shownMonth + delta, 1);
            shownYear = next.getFullYear();
            shownMonth = next.getMonth();
        }

        function choose(date) {
            control.picked(control.iso(date));
            close();
        }

        y: control.height + 4
        width: 280
        padding: 10
        margins: 8 // keeps the popup inside the window
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onAboutToShow: {
            shownYear = control.selected.getFullYear();
            shownMonth = control.selected.getMonth();
        }

        background: Rectangle {
            radius: Theme.radiusControl
            color: Qt.rgba(0.07, 0.05, 0.12, 0.97)
            border.width: 1
            border.color: Theme.glassStroke
        }

        contentItem: Column {
            spacing: 6

            Item {
                width: parent.width
                height: 32

                IconButton {
                    anchors.left: parent.left
                    glyph: Icons.arrowLeft
                    flat: true
                    implicitWidth: 32
                    implicitHeight: 32
                    onClicked: popup.step(-1)
                }

                Text {
                    anchors.centerIn: parent
                    text: Qt.locale().monthName(popup.shownMonth) + " " + popup.shownYear
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody + 1
                    font.weight: Font.DemiBold
                }

                IconButton {
                    anchors.right: parent.right
                    glyph: Icons.arrowRight
                    flat: true
                    implicitWidth: 32
                    implicitHeight: 32
                    onClicked: popup.step(1)
                }
            }

            DayOfWeekRow {
                width: parent.width
                locale: Qt.locale()
                delegate: Text {
                    required property string shortName

                    text: shortName
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            MonthGrid {
                id: grid

                width: parent.width
                month: popup.shownMonth
                year: popup.shownYear
                locale: Qt.locale()
                onClicked: date => popup.choose(date)

                delegate: Rectangle {
                    id: day

                    required property var model
                    readonly property bool inMonth: model.month === grid.month
                    readonly property bool chosen: control.value.length > 0 && control.iso(model.date) === control.value

                    implicitWidth: 36
                    implicitHeight: 32
                    radius: Theme.radiusSmall
                    color: chosen ? Theme.withAlpha(Theme.accent, 0.35) : dayHover.hovered ? Theme.rowHover : "transparent"
                    border.width: model.today ? 1 : 0
                    border.color: Theme.withAlpha(Theme.accent, 0.7)

                    Text {
                        anchors.centerIn: parent
                        text: day.model.day
                        color: day.inMonth ? Theme.textPrimary : Theme.textMuted
                        opacity: day.inMonth ? 1 : 0.45
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                    }

                    HoverHandler {
                        id: dayHover
                    }
                }
            }

            Row {
                spacing: Theme.spacingS

                SecondaryButton {
                    implicitHeight: 32
                    text: qsTr("Today")
                    onClicked: popup.choose(new Date())
                }

                SecondaryButton {
                    implicitHeight: 32
                    text: qsTr("Clear")
                    enabled: control.value.length > 0
                    onClicked: {
                        control.picked("");
                        popup.close();
                    }
                }
            }
        }
    }
}
