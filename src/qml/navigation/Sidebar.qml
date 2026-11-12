import QtQuick

// Floating glass sidebar: brand, section navigation with the liquid selection pill, and a
// live background-jobs card at the bottom that leads to the Jobs section.
FocusScope {
    id: sidebar

    property var sections: []
    property int currentIndex: 0
    property int runningJobs: 0
    property int activeJobs: 0
    property string databaseState: "ready" // "connecting", "ready", "failed"
    property int jobsSection: 3

    // origin: center of the clicked icon in sidebar coordinates (start of the mood spread)
    signal activated(int index, point origin)

    readonly property int itemHeight: 46
    readonly property int itemSpacing: 6

    function originFor(index) {
        const item = repeater.itemAt(index);
        if (!item)
            return Qt.point(width / 2, nav.y);
        return item.mapToItem(sidebar, item.iconCenter.x, item.iconCenter.y);
    }

    function select(index) {
        if (index < 0 || index >= sections.length)
            return;
        activated(index, originFor(index));
    }

    function indicatorTop(index) {
        return index * (itemHeight + itemSpacing);
    }

    onCurrentIndexChanged: indicator.moveTo(indicatorTop(currentIndex), indicatorTop(currentIndex) + itemHeight)
    Component.onCompleted: indicator.jumpTo(indicatorTop(currentIndex), indicatorTop(currentIndex) + itemHeight)

    Keys.onUpPressed: select(Math.max(0, currentIndex - 1))
    Keys.onDownPressed: select(Math.min(sections.length - 1, currentIndex + 1))

    GlassPanel {
        anchors.fill: parent
        tintStrength: 1
    }

    Row {
        id: brand

        x: 22
        y: 22
        spacing: Theme.spacingM

        Rectangle {
            width: 30
            height: 30
            radius: 10
            anchors.verticalCenter: parent.verticalCenter
            gradient: Gradient {
                GradientStop { position: 0; color: Theme.accent }
                GradientStop { position: 1; color: Theme.accentSecondary }
            }

            Icon {
                anchors.centerIn: parent
                glyph: Icons.target
                filled: true
                size: 17
                color: "#160B24"
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "Hyperlancer"
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: 17
            font.weight: Font.Bold
            font.letterSpacing: 0.3
        }
    }

    Text {
        x: 24
        y: nav.y - 26
        text: qsTr("WORKSPACE")
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: 10
        font.weight: Font.DemiBold
        font.letterSpacing: 1.4
    }

    Item {
        id: nav

        x: 12
        y: 110
        width: parent.width - 24
        height: sidebar.sections.length * (sidebar.itemHeight + sidebar.itemSpacing)

        LiquidIndicator {
            id: indicator

            width: parent.width
        }

        Column {
            width: parent.width
            spacing: sidebar.itemSpacing

            Repeater {
                id: repeater

                model: sidebar.sections

                delegate: SidebarItem {
                    required property int index
                    required property var modelData

                    width: nav.width
                    height: sidebar.itemHeight
                    text: modelData.title
                    glyph: modelData.glyph
                    shortcutHint: "Ctrl+" + (index + 1)
                    selected: index === sidebar.currentIndex
                    onClicked: sidebar.select(index)
                }
            }
        }
    }

    Rectangle {
        id: jobsCard

        readonly property bool busy: sidebar.activeJobs > 0
        readonly property bool dbProblem: sidebar.databaseState === "failed"

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        height: 64
        radius: 14
        color: jobsHover.hovered ? Theme.glassFillHover : Theme.glassFill
        border.width: 1
        border.color: jobsCard.dbProblem ? Theme.withAlpha(Theme.danger, 0.55)
                      : jobsCard.busy ? Theme.withAlpha(Theme.accent, 0.4) : Theme.glassStroke

        Behavior on color { ColorAnimation { duration: Motion.fast } }
        Behavior on border.color { ColorAnimation { duration: Motion.normal } }

        Item {
            id: jobsIconSlot

            x: 14
            width: 22
            height: 22
            anchors.verticalCenter: parent.verticalCenter

            Icon {
                id: jobsIcon

                anchors.centerIn: parent
                glyph: jobsCard.dbProblem ? Icons.warningCircle : jobsCard.busy ? Icons.spinner : Icons.queue
                size: 18
                color: jobsCard.dbProblem ? Theme.danger : Theme.accent

                RotationAnimation on rotation {
                    running: jobsCard.busy && !jobsCard.dbProblem && !Motion.reducedMotion && sidebar.visible
                    from: 0
                    to: 360
                    duration: 1400
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) jobsIcon.rotation = 0
                }
            }
        }

        Column {
            anchors.left: jobsIconSlot.right
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1

            Text {
                width: parent.width
                text: jobsCard.dbProblem ? qsTr("Database offline")
                      : jobsCard.busy ? qsTr("%n running", "", sidebar.runningJobs)
                      : qsTr("Background jobs")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.weight: Font.Medium
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: jobsCard.dbProblem ? qsTr("Open Settings → Database")
                      : jobsCard.busy ? qsTr("%n in the queue", "", Math.max(0, sidebar.activeJobs - sidebar.runningJobs))
                      : sidebar.databaseState === "connecting" ? qsTr("Connecting…") : qsTr("All quiet")
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
        }

        HoverHandler {
            id: jobsHover

            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            onTapped: sidebar.select(jobsCard.dbProblem ? sidebar.sections.length - 1 : sidebar.jobsSection)
        }
    }
}
