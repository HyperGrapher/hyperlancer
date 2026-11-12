import QtQuick
import QtQuick.Layouts
import Hyperlancer

// Discover leads through web search (LangSearch). A query returns one row per website;
// websites already tracked are marked, new ones are pre-selected, and the selection is added
// to Sites, optionally queued for analysis. Earlier searches are listed on the right.
PageBase {
    id: page

    required property AppContext app

    readonly property Discovery discovery: app.discovery
    readonly property var countOptions: [10, 20, 50]
    readonly property var freshnessOptions: ["noLimit", "oneYear", "oneMonth"]
    property int countIndex: 1
    property int freshnessIndex: 0

    signal settingsRequested

    function runSearch() {
        if (queryField.text.trim().length === 0)
            return;
        discovery.search(queryField.text, countOptions[countIndex], freshnessOptions[freshnessIndex]);
    }

    PageHeader {
        id: header

        width: parent.width
        title: qsTr("Discover")
        subtitle: qsTr("Search the web for businesses whose websites need work, then add them to Sites.")
        opacity: page.reveal(0)
        transform: Translate { y: page.revealShift(0) }
    }

    GlassPanel {
        id: searchPanel

        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingXl
        width: parent.width
        height: searchColumn.implicitHeight + 2 * Theme.spacingL + 4
        radius: Theme.radiusCard
        opacity: page.reveal(1)
        transform: Translate { y: page.revealShift(1) }

        Column {
            id: searchColumn

            x: Theme.spacingL + 4
            y: Theme.spacingL + 2
            width: parent.width - 2 * (Theme.spacingL + 4)
            spacing: Theme.spacingM

            RowLayout {
                width: parent.width
                spacing: Theme.spacingS

                StyledTextField {
                    id: queryField

                    Layout.fillWidth: true
                    implicitHeight: 44
                    font.pixelSize: Theme.fontBody + 2
                    glyph: Icons.search
                    clearable: true
                    placeholderText: qsTr("Niche and place, e.g. diş kliniği Kadıköy")
                    onAccepted: page.runSearch()
                }

                SegmentedControl {
                    implicitHeight: 44
                    options: page.countOptions.map(n => String(n))
                    currentIndex: page.countIndex
                    onActivated: index => page.countIndex = index
                }

                SegmentedControl {
                    implicitHeight: 44
                    options: [qsTr("Any time"), qsTr("Year"), qsTr("Month")]
                    currentIndex: page.freshnessIndex
                    onActivated: index => page.freshnessIndex = index
                }

                PrimaryButton {
                    implicitHeight: 44
                    implicitWidth: 128
                    text: page.discovery.busy ? qsTr("Searching") : qsTr("Search")
                    glyph: Icons.binoculars
                    enabled: !page.discovery.busy && queryField.text.trim().length > 0
                             && page.app.integrations.hasLangSearchKey && page.app.database.state === Database.Ready
                    onClicked: page.runSearch()
                }
            }

        }

        ThinProgress {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.radiusCard
            anchors.rightMargin: Theme.radiusCard
            height: 2
            value: -1
            visible: page.discovery.busy
        }
    }

    RowLayout {
        anchors.top: searchPanel.bottom
        anchors.topMargin: Theme.spacingL
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: Theme.spacingL

        GlassPanel {
            id: resultsPanel

            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusCard
            clip: true
            opacity: page.reveal(2)
            transform: Translate { y: page.revealShift(2) }

            Item {
                id: resultsBar

                width: parent.width
                height: 56
                visible: page.discovery.count > 0

                Column {
                    x: Theme.spacingL + 2
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Text {
                        text: qsTr("%n website(s)", "", page.discovery.count) + "  ·  " + qsTr("%n new", "", page.discovery.newCount)
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontHeading
                        font.weight: Font.DemiBold
                    }

                    Text {
                        text: "“" + page.discovery.lastQuery + "”"
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption + 1
                        elide: Text.ElideRight
                        width: resultsPanel.width * 0.4
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingM
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingS

                    SecondaryButton {
                        text: page.discovery.selectedCount > 0 ? qsTr("Clear") : qsTr("Select new")
                        onClicked: page.discovery.selectedCount > 0 ? page.discovery.clearSelection() : page.discovery.selectAllNew()
                    }

                    SecondaryButton {
                        text: qsTr("Add %1").arg(page.discovery.selectedCount)
                        glyph: Icons.plus
                        enabled: page.discovery.selectedCount > 0
                        onClicked: page.discovery.importSelected(false)
                    }

                    PrimaryButton {
                        implicitHeight: 40
                        implicitWidth: 176
                        text: qsTr("Add and analyze")
                        glyph: Icons.lightning
                        enabled: page.discovery.selectedCount > 0
                        onClicked: page.discovery.importSelected(true)
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.glassStroke
                }
            }

            ListView {
                id: results

                anchors.top: resultsBar.visible ? resultsBar.bottom : parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 6
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: page.discovery

                add: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.normal }
                }

                delegate: Item {
                    id: result

                    required property int index
                    required property string url
                    required property string domain
                    required property string title
                    required property string snippet
                    required property bool known
                    required property bool selected
                    required property bool imported
                    required property bool duplicate
                    readonly property bool selectable: !known && !imported

                    width: results.width
                    height: 62
                    opacity: page.reveal(3 + Math.min(index, 5))

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusSmall
                        color: result.selected ? Theme.withAlpha(Theme.accent, 0.09) : resultHover.hovered ? Theme.rowHover : "transparent"

                        Behavior on color { ColorAnimation { duration: Motion.fast } }
                    }

                    // Check box
                    Rectangle {
                        id: box

                        x: Theme.spacingM + 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: 18
                        height: 18
                        radius: 5
                        opacity: result.selectable ? 1 : 0.3
                        color: result.selected ? Theme.accent : "transparent"
                        border.width: result.selected ? 0 : 1.5
                        border.color: resultHover.hovered ? Theme.accent : Theme.glassStrokeStrong

                        Behavior on color { ColorAnimation { duration: Motion.fast } }

                        Icon {
                            anchors.centerIn: parent
                            glyph: Icons.check
                            size: 12
                            color: "#1E0A12"
                            scale: result.selected ? 1 : 0.4
                            opacity: result.selected ? 1 : 0

                            Behavior on scale { NumberAnimation { duration: Motion.normal; easing.type: Easing.OutBack } }
                            Behavior on opacity { NumberAnimation { duration: Motion.fast } }
                        }
                    }

                    SiteBadge {
                        id: resultBadge

                        anchors.left: box.right
                        anchors.leftMargin: Theme.spacingM
                        anchors.verticalCenter: parent.verticalCenter
                        siteKey: result.domain
                        size: 30
                    }

                    Column {
                        anchors.left: resultBadge.right
                        anchors.leftMargin: Theme.spacingM
                        anchors.right: stateBadge.left
                        anchors.rightMargin: Theme.spacingL
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3

                        Text {
                            width: parent.width
                            text: result.domain + (result.title.length > 0 ? "  —  " + result.title : "")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody + 1
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }

                        Text {
                            width: parent.width
                            text: result.snippet
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontCaption + 1
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }
                    }

                    Badge {
                        id: stateBadge

                        anchors.right: openButton.left
                        anchors.rightMargin: Theme.spacingS
                        anchors.verticalCenter: parent.verticalCenter
                        text: result.imported ? qsTr("Added") : result.known ? qsTr("Known") : result.duplicate ? qsTr("Same site") : qsTr("New")
                        glyph: result.imported ? Icons.checkCircle : ""
                        tint: result.imported ? Theme.success : result.known || result.duplicate ? Theme.textMuted : Theme.accent
                    }

                    IconButton {
                        id: openButton

                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacingS
                        anchors.verticalCenter: parent.verticalCenter
                        flat: true
                        glyph: Icons.openExternal
                        hint: qsTr("Open in browser")
                        onClicked: page.app.openExternal(result.url)
                    }

                    Rectangle {
                        anchors.left: resultBadge.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.rowDivider
                    }

                    HoverHandler {
                        id: resultHover
                    }

                    TapHandler {
                        onTapped: page.discovery.toggle(result.index)
                    }
                }
            }

            EmptyState {
                anchors.centerIn: parent
                width: parent.width
                visible: page.discovery.count === 0 && !page.discovery.busy
                glyph: !page.app.integrations.hasLangSearchKey ? Icons.key : page.discovery.error.length > 0 ? Icons.warningCircle : Icons.binoculars
                title: !page.app.integrations.hasLangSearchKey ? qsTr("Add your LangSearch API key")
                       : page.discovery.error.length > 0 ? qsTr("Search failed")
                       : page.discovery.lastQuery.length > 0 ? qsTr("No websites found") : qsTr("Find your next clients")
                text: !page.app.integrations.hasLangSearchKey ? qsTr("Discover uses the LangSearch web search API. The key is stored in Windows Credential Manager.")
                      : page.discovery.error.length > 0 ? page.discovery.error
                      : page.discovery.lastQuery.length > 0 ? qsTr("Try a broader query or another phrase.")
                      : qsTr("Search a niche and a place, like “diş kliniği Kadıköy”, or an old footer phrase.")

                SecondaryButton {
                    visible: !page.app.integrations.hasLangSearchKey
                    text: qsTr("Open Connections")
                    glyph: Icons.key
                    onClicked: page.settingsRequested()
                }
            }
        }

        SectionPanel {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            title: qsTr("Recent searches")
            glyph: Icons.history
            clip: true
            opacity: page.reveal(3)
            transform: Translate { y: page.revealShift(3) }

            Repeater {
                model: page.discovery.recentRuns

                delegate: Item {
                    id: run

                    required property var modelData
                    required property int index

                    width: parent.width
                    height: 54

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: -8
                        anchors.rightMargin: -8
                        radius: Theme.radiusSmall
                        color: runHover.hovered ? Theme.rowHover : "transparent"
                    }

                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3

                        Text {
                            width: parent.width
                            text: run.modelData.query
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: qsTr("%1 sites · %2 new · %3").arg(run.modelData.resultCount).arg(run.modelData.newCount)
                                  .arg(Format.relativeTime(run.modelData.createdAt))
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontCaption
                            elide: Text.ElideRight
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.rowDivider
                        visible: run.index < page.discovery.recentRuns.length - 1
                    }

                    HoverHandler {
                        id: runHover

                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        onTapped: {
                            queryField.text = run.modelData.query;
                            page.discovery.openRun(run.modelData.id);
                        }
                    }
                }
            }

            Text {
                width: parent.width
                visible: page.discovery.recentRuns.length === 0
                topPadding: Theme.spacingS
                text: qsTr("Searches you run are kept here, with their results.")
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
            }
        }
    }
}
