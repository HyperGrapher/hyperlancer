import QtQuick
import QtQuick.Layouts
import Hyperlancer

// Sites: a searchable table of every tracked website, and the detail view of one site that
// slides in over it. Rows are standard table rows: one line per site, columns that line up.
PageBase {
    id: page

    required property AppContext app
    property bool ambientActive: true
    property bool detailOpen: false

    readonly property bool wide: width > 1020
    readonly property var filterNames: [qsTr("All"), qsTr("Not analyzed"), qsTr("Unreachable"), qsTr("Expiring")]

    function openSite(siteId) {
        app.siteDetail.siteId = siteId;
        detailOpen = true;
    }

    function closeDetail() {
        detailOpen = false;
        app.sites.refreshSoon();
    }

    function submit() {
        if (addField.text.trim().length > 0)
            app.addSite(addField.text, true);
    }

    Connections {
        target: page.app

        function onSiteAdded(siteId, created) {
            if (!page.isCurrent)
                return;
            addField.clear();
            page.openSite(siteId);
        }

        function onSiteRemoved(siteId) {
            if (page.app.siteDetail.siteId === siteId)
                page.closeDetail();
        }
    }

    Keys.onEscapePressed: if (detailOpen) closeDetail()

    // ---- list layer --------------------------------------------------------------------
    Item {
        id: listLayer

        width: parent.width
        height: parent.height
        enabled: !page.detailOpen
        opacity: page.detailOpen ? 0 : 1
        x: page.detailOpen ? -28 : 0
        visible: opacity > 0.001

        Behavior on opacity { NumberAnimation { duration: Motion.normal; easing.type: Easing.OutCubic } }
        Behavior on x { NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic } }

        PageHeader {
            id: header

            width: parent.width
            title: qsTr("Sites")
            subtitle: page.app.sites.total === 0 ? qsTr("Every website you track, with what its last analysis found.")
                      : qsTr("%1 of %n site(s)", "", page.app.sites.total).arg(page.app.sites.count)
            opacity: page.reveal(0)
            transform: Translate { y: page.revealShift(0) }

            StyledTextField {
                id: addField

                width: page.wide ? 300 : 220
                glyph: Icons.plus
                placeholderText: qsTr("Add a website…")
                onAccepted: page.submit()
            }

            PrimaryButton {
                implicitHeight: Theme.inputHeight
                implicitWidth: 124
                text: qsTr("Analyze")
                glyph: Icons.lightning
                enabled: addField.text.trim().length > 0 && page.app.database.state === Database.Ready
                onClicked: page.submit()
            }
        }

        Item {
            id: toolbar

            anchors.top: header.bottom
            anchors.topMargin: Theme.spacingL + 2
            width: parent.width
            height: Theme.inputHeight
            opacity: page.reveal(1)
            transform: Translate { y: page.revealShift(1) }

            StyledTextField {
                id: searchField

                width: Math.min(340, parent.width * 0.36)
                glyph: Icons.search
                clearable: true
                placeholderText: qsTr("Search domain, title, CMS or host")
                onTextChanged: page.app.sites.search = text
            }

            SegmentedControl {
                anchors.left: searchField.right
                anchors.leftMargin: Theme.spacingM
                anchors.verticalCenter: parent.verticalCenter
                implicitHeight: Theme.inputHeight
                options: page.filterNames
                currentIndex: page.app.sites.filter
                onActivated: index => page.app.sites.filter = index
            }
        }

        GlassPanel {
            id: table

            anchors.top: toolbar.bottom
            anchors.topMargin: Theme.spacingM
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            radius: Theme.radiusCard
            clip: true
            opacity: page.reveal(2)
            transform: Translate { y: page.revealShift(2) }

            TableHeader {
                id: tableHeader

                width: parent.width
                horizontalPadding: Theme.spacingL + 2

                TableHeader.Caption { text: qsTr("Site"); Layout.fillWidth: true; Layout.minimumWidth: 200; Layout.leftMargin: 44 }
                TableHeader.Caption { text: qsTr("CMS"); Layout.preferredWidth: 110 }
                TableHeader.Caption { text: qsTr("Hosting"); Layout.preferredWidth: 140; visible: page.wide }
                TableHeader.Caption { text: qsTr("HTTP"); Layout.preferredWidth: 46 }
                TableHeader.Caption { text: qsTr("SSL"); Layout.preferredWidth: 64 }
                TableHeader.Caption { text: qsTr("Domain"); Layout.preferredWidth: 64; visible: page.wide }
                TableHeader.Caption { text: qsTr("Analyzed"); Layout.preferredWidth: 128 }
                Item { Layout.preferredWidth: 72 }
            }

            ListView {
                id: list

                anchors.top: tableHeader.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 6
                anchors.topMargin: 4
                clip: true
                model: page.app.sites
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true
                cacheBuffer: 400

                add: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.normal }
                }

                delegate: SiteTableRow {
                    width: list.width
                    wide: page.wide
                    onOpened: siteId => page.openSite(siteId)
                    onAnalyzeRequested: (siteId, url) => page.app.analyzeSite(siteId, url)
                    onExternalRequested: url => page.app.openExternal(url)
                }
            }

            EmptyState {
                anchors.centerIn: list
                width: list.width
                visible: page.app.sites.loaded && page.app.sites.count === 0
                glyph: page.app.sites.total === 0 ? Icons.globe : Icons.funnel
                title: page.app.sites.total === 0 ? qsTr("No sites yet") : qsTr("Nothing matches")
                text: page.app.sites.total === 0
                      ? qsTr("Add a website above, or find businesses in Discover and import them here.")
                      : qsTr("Try another search or filter.")
            }
        }
    }

    // ---- detail layer ------------------------------------------------------------------
    SiteDetailView {
        id: detailLayer

        width: parent.width
        height: parent.height
        app: page.app
        ambientActive: page.ambientActive
        enabled: page.detailOpen
        opacity: page.detailOpen ? 1 : 0
        x: page.detailOpen ? 0 : 36
        visible: opacity > 0.001
        onBackRequested: page.closeDetail()

        Behavior on opacity { NumberAnimation { duration: Motion.normal + 40; easing.type: Easing.OutCubic } }
        Behavior on x { NumberAnimation { duration: Motion.slow; easing.type: Easing.OutCubic } }
    }
}
