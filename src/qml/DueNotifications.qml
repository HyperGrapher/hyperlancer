import QtQuick
import QtQuick.Layouts
import Hyperlancer

// Reminders for website deadlines and to-dos that are due today or overdue, in a small
// always-on-top window in the top-right corner, below the visitor notices. A notice stays
// until the user dismisses it.
Window {
    id: popup

    required property AppContext app
    // Space taken above by the visitor notices.
    property real topOffset: 0

    // Newest first: { key, kind, title, siteKey, siteId, overdue, dueOn }
    property var pending: []
    readonly property int maxShown: 4
    readonly property int cardHeight: 92
    readonly property int headerHeight: pending.length > 1 ? 38 : 0

    signal openRequested(var siteId)

    function dismiss(key) {
        pending = pending.filter(item => item.key !== key);
    }

    function heading(item) {
        if (item.kind === "deadline")
            return item.overdue ? qsTr("Deadline passed") : qsTr("Deadline today");
        return item.overdue ? qsTr("To-do overdue") : qsTr("To-do due today");
    }

    transientParent: null
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: pending.length > 0
    width: 392
    height: Math.min(pending.length, maxShown) * (cardHeight + 8) + headerHeight + 16
    x: Screen.virtualX + Screen.desktopAvailableWidth - width - 8
    y: Screen.virtualY + 8 + topOffset

    Connections {
        target: popup.app.dueNotifier

        function onDueItems(items) {
            const fresh = items.map(item => ({
                key: item.kind + ":" + item.id,
                kind: item.kind,
                title: item.title,
                siteKey: item.siteKey,
                siteId: item.siteId,
                overdue: item.overdue,
                dueOn: item.dueOn
            }));
            popup.pending = fresh.concat(popup.pending.filter(old => !fresh.some(item => item.key === old.key))).slice(0, 30);
        }
    }

    Column {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        Rectangle {
            width: parent.width
            height: popup.headerHeight
            visible: popup.headerHeight > 0
            radius: Theme.radiusControl
            color: Qt.rgba(0.07, 0.05, 0.12, 0.97)
            border.width: 1
            border.color: Theme.glassStroke

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingS
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("%n reminders", "", popup.pending.length)
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody + 1
                font.weight: Font.DemiBold
            }

            SecondaryButton {
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                implicitHeight: 28
                text: qsTr("Dismiss all")
                onClicked: popup.pending = []
            }
        }

        Repeater {
            model: popup.pending.slice(0, popup.maxShown)

            delegate: Rectangle {
                id: card

                required property var modelData

                width: parent.width
                height: popup.cardHeight
                radius: Theme.radiusCard
                color: Qt.rgba(0.07, 0.05, 0.12, 0.97)
                border.width: 1
                border.color: Theme.withAlpha(card.modelData.overdue ? Theme.danger : Theme.warning, 0.6)

                Icon {
                    id: icon

                    x: Theme.spacingM
                    y: Theme.spacingM
                    glyph: card.modelData.kind === "deadline" ? Icons.calendar : Icons.checkSquare
                    size: 18
                    color: card.modelData.overdue ? Theme.danger : Theme.warning
                }

                Column {
                    anchors.left: icon.right
                    anchors.leftMargin: Theme.spacingM
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingM
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spacingM - 2
                    spacing: 2

                    Text {
                        width: parent.width
                        text: popup.heading(card.modelData) + "  ·  " + Format.shortDate(card.modelData.dueOn)
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody + 1
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        text: card.modelData.kind === "todo" ? card.modelData.title + "  ·  " + card.modelData.siteKey : card.modelData.siteKey
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingM
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.spacingS
                    spacing: Theme.spacingS

                    SecondaryButton {
                        implicitHeight: 30
                        text: qsTr("Dismiss")
                        onClicked: popup.dismiss(card.modelData.key)
                    }

                    PrimaryButton {
                        implicitHeight: 30
                        implicitWidth: 96
                        text: qsTr("Open")
                        onClicked: {
                            popup.openRequested(card.modelData.siteId);
                            popup.dismiss(card.modelData.key);
                        }
                    }
                }
            }
        }
    }
}
