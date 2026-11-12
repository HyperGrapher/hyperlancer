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

    readonly property bool wide: width > 940
    readonly property var statusFilterOptions: [{ value: "", label: qsTr("Any status") }].concat(Format.leadStatusOptions)
    readonly property var categoryFilterOptions: [{ value: "", label: qsTr("Any category") }]
        .concat(app.sites.categories.map(name => ({ value: name, label: name })))
    readonly property var healthFilterOptions: [{ value: 0, label: qsTr("All websites") },
                                                { value: 1, label: qsTr("Unreachable") },
                                                { value: 2, label: qsTr("Expiring") }]

    // Clicking the active column flips the direction; a new column starts ascending, except
    // "Analyzed", where newest first is the useful order.
    function sortBy(key) {
        const sites = app.sites;
        if (sites.sortKey === key) {
            sites.sortDescending = !sites.sortDescending;
        } else {
            sites.sortKey = key;
            sites.sortDescending = key === 0;
        }
    }

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
            title: qsTr("Websites")
            subtitle: page.app.sites.total === 0 ? qsTr("Every website you track, with what its last analysis found.")
                      : qsTr("%1 of %n website(s)", "", page.app.sites.total).arg(page.app.sites.count)
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

            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingM

                StyledTextField {
                    id: searchField

                    Layout.fillWidth: true
                    Layout.minimumWidth: 180
                    Layout.preferredWidth: 340
                    Layout.maximumWidth: 380
                    glyph: Icons.search
                    clearable: true
                    placeholderText: qsTr("Search domain, title, tag or category")
                    onTextChanged: page.app.sites.search = text
                }

                Item { Layout.fillWidth: true }

                Dropdown {
                    Layout.preferredWidth: 150
                    options: page.statusFilterOptions
                    currentValue: page.app.sites.statusFilter
                    onActivated: value => page.app.sites.statusFilter = value
                }

                Dropdown {
                    Layout.preferredWidth: 160
                    options: page.categoryFilterOptions
                    currentValue: page.app.sites.categoryFilter
                    searchable: true
                    onActivated: value => page.app.sites.categoryFilter = value
                }

                Dropdown {
                    Layout.preferredWidth: 150
                    options: page.healthFilterOptions
                    currentValue: page.app.sites.filter
                    onActivated: value => page.app.sites.filter = value
                }
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

                TableHeader.SortCaption { activeKey: page.app.sites.sortKey; descending: page.app.sites.sortDescending; onSortRequested: key => page.sortBy(key); baseText: qsTr("Site"); sortKey: 1; Layout.fillWidth: true; Layout.minimumWidth: 160; Layout.leftMargin: 44 }
                TableHeader.SortCaption { activeKey: page.app.sites.sortKey; descending: page.app.sites.sortDescending; onSortRequested: key => page.sortBy(key); baseText: qsTr("Status"); sortKey: 2; Layout.preferredWidth: 124 }
                TableHeader.SortCaption { activeKey: page.app.sites.sortKey; descending: page.app.sites.sortDescending; onSortRequested: key => page.sortBy(key); baseText: qsTr("Category"); sortKey: 3; Layout.preferredWidth: 104 }
                TableHeader.Caption { text: qsTr("Tags"); Layout.preferredWidth: 120; visible: page.wide }
                TableHeader.SortCaption { activeKey: page.app.sites.sortKey; descending: page.app.sites.sortDescending; onSortRequested: key => page.sortBy(key); baseText: qsTr("Deadline"); sortKey: 4; Layout.preferredWidth: 84 }
                TableHeader.SortCaption { activeKey: page.app.sites.sortKey; descending: page.app.sites.sortDescending; onSortRequested: key => page.sortBy(key); baseText: qsTr("To-dos"); sortKey: 5; Layout.preferredWidth: 46 }
                TableHeader.SortCaption { activeKey: page.app.sites.sortKey; descending: page.app.sites.sortDescending; onSortRequested: key => page.sortBy(key); baseText: qsTr("Analyzed"); sortKey: 0; Layout.preferredWidth: 104 }
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
                    onStatusRequested: (siteId, status) => page.app.sites.setSiteStatus(siteId, status)
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
                title: page.app.sites.total === 0 ? qsTr("No websites yet") : qsTr("Nothing matches")
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
