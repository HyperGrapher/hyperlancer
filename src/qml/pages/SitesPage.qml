import QtQuick
import QtQuick.Controls
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

    // Used by the verification tour: switches the open website to a tab, -1 closes it.
    function showDetailTab(tab) {
        if (tab < 0) {
            closeDetail();
            return;
        }
        if (tab >= 10) {
            detailLayer.showWorkspacePart(["deadline", "tags", "note"][tab - 10]);
            return;
        }
        detailLayer.currentTab = tab;
        if (tab === 0 && app.siteDetail.siteId > 0) {
            // Gives the sample site some organizer data so the tabs have something to show.
            const detail = app.siteDetail;
            detail.setStatus("contacted");
            detail.setCategory("Dental clinic");
            detail.addTag("vip");
            detail.addTag("slow site");
            detail.setDeadline(Qt.formatDate(new Date(Date.now() + 9 * 86400000), "yyyy-MM-dd"));
            detail.addTodo("Send the first offer", Qt.formatDate(new Date(Date.now() + 3 * 86400000), "yyyy-MM-dd"));
            detail.addNote("Call notes", "## Owner\n- Prefers **email**\n- Site is on an old theme\n\nSee [example](https://example.org).");
        }
    }

    function closeDetail() {
        detailOpen = false;
        detailLayer.resetScroll();
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

            IconButton {
                implicitWidth: Theme.inputHeight
                implicitHeight: Theme.inputHeight
                glyph: Icons.queue
                hint: qsTr("Add many websites")
                enabled: page.app.database.state === Database.Ready
                onClicked: addMany.open()
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

        // Bulk actions: replaces the filters while websites are selected.
        Rectangle {
            id: bulkBar

            anchors.fill: toolbar
            radius: Theme.radiusControl
            color: Qt.rgba(0.06, 0.04, 0.11, 1)
            border.width: 1
            border.color: Theme.withAlpha(Theme.accent, 0.5)
            visible: page.app.sites.selectedCount > 0
            z: 2

            // Swallows clicks so nothing behind the bar reacts.
            MouseArea {
                anchors.fill: parent
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingM
                anchors.rightMargin: Theme.spacingS
                spacing: Theme.spacingS

                Text {
                    text: qsTr("%n selected", "", page.app.sites.selectedCount)
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody + 1
                    font.weight: Font.DemiBold
                    Layout.rightMargin: Theme.spacingS
                }

                Dropdown {
                    Layout.preferredWidth: 150
                    compact: true
                    placeholder: qsTr("Set status…")
                    options: Format.leadStatusOptions
                    currentValue: ""
                    onActivated: value => page.app.sites.setStatusForSelected(value)
                }

                Dropdown {
                    Layout.preferredWidth: 160
                    compact: true
                    placeholder: qsTr("Set category…")
                    options: [{ value: "", label: qsTr("No category") }].concat(page.app.sites.categories.map(name => ({ value: name, label: name })))
                    currentValue: "\u0000"
                    searchable: true
                    allowCreate: true
                    createLabel: qsTr("New category “%1”")
                    onActivated: value => page.app.sites.setCategoryForSelected(value)
                    onCreated: text => page.app.sites.setCategoryForSelected(text)
                }

                Dropdown {
                    Layout.preferredWidth: 150
                    compact: true
                    placeholder: qsTr("Add tag…")
                    options: page.app.sites.tags.map(name => ({ value: name, label: name }))
                    currentValue: "\u0000"
                    searchable: true
                    allowCreate: true
                    createLabel: qsTr("New tag “%1”")
                    onActivated: value => page.app.sites.addTagToSelected(value)
                    onCreated: text => page.app.sites.addTagToSelected(text)
                }

                Item { Layout.fillWidth: true }

                SecondaryButton {
                    implicitHeight: 32
                    text: qsTr("Analyze")
                    glyph: Icons.lightning
                    onClicked: page.app.sites.analyzeSelected()
                }

                SecondaryButton {
                    implicitHeight: 32
                    text: qsTr("Remove")
                    glyph: Icons.trash
                    onClicked: removeConfirm.open()
                }

                IconButton {
                    implicitWidth: 32
                    implicitHeight: 32
                    glyph: Icons.close
                    hint: qsTr("Clear selection")
                    onClicked: page.app.sites.clearSelection()
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

                // Selects or clears every website in the list.
                Rectangle {
                    readonly property bool all: page.app.sites.count > 0 && page.app.sites.selectedCount === page.app.sites.count

                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    Layout.alignment: Qt.AlignVCenter
                    radius: 6
                    color: all ? Theme.withAlpha(Theme.accent, 0.35) : "transparent"
                    border.width: 1.5
                    border.color: all || page.app.sites.selectedCount > 0 ? Theme.accent : Theme.textMuted

                    Icon {
                        anchors.centerIn: parent
                        glyph: parent.all ? Icons.check : Icons.minus
                        size: 13
                        color: Theme.accent
                        visible: parent.all || page.app.sites.selectedCount > 0
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: parent.all || page.app.sites.selectedCount > 0 ? page.app.sites.clearSelection() : page.app.sites.selectAll()
                    }
                }

                TableHeader.SortCaption { activeKey: page.app.sites.sortKey; descending: page.app.sites.sortDescending; onSortRequested: key => page.sortBy(key); baseText: qsTr("Site"); sortKey: 1; Layout.fillWidth: true; Layout.minimumWidth: 160; Layout.leftMargin: 44 }
                TableHeader.SortCaption { activeKey: page.app.sites.sortKey; descending: page.app.sites.sortDescending; onSortRequested: key => page.sortBy(key); baseText: qsTr("Status"); sortKey: 2; Layout.preferredWidth: 124 }
                TableHeader.SortCaption { activeKey: page.app.sites.sortKey; descending: page.app.sites.sortDescending; onSortRequested: key => page.sortBy(key); baseText: qsTr("Category"); sortKey: 3; Layout.preferredWidth: 104 }
                TableHeader.Caption { text: qsTr("Tags"); Layout.preferredWidth: 120; visible: page.wide }
                TableHeader.SortCaption { activeKey: page.app.sites.sortKey; descending: page.app.sites.sortDescending; onSortRequested: key => page.sortBy(key); baseText: qsTr("Deadline"); sortKey: 4; Layout.preferredWidth: 84 }
                TableHeader.SortCaption { activeKey: page.app.sites.sortKey; descending: page.app.sites.sortDescending; onSortRequested: key => page.sortBy(key); baseText: qsTr("To-dos"); sortKey: 5; Layout.preferredWidth: 60 }
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
                    onSelectionToggled: siteId => page.app.sites.toggleSelected(siteId)
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

    // ---- add many websites -------------------------------------------------------------
    Popup {
        id: addMany

        property string result: ""

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(560, page.width - 80)
        modal: true
        padding: Theme.spacingXl
        closePolicy: Popup.CloseOnEscape
        onOpened: {
            result = "";
            manyField.forceActiveFocus();
        }

        background: Rectangle {
            radius: Theme.radiusCard
            color: Qt.rgba(0.07, 0.05, 0.12, 0.98)
            border.width: 1
            border.color: Theme.glassStroke

            MouseArea {
                anchors.fill: parent
            }
        }

        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0.03, 0.55)
        }

        Connections {
            target: page.app

            function onSitesAdded(added, existing, invalid) {
                const parts = [];
                if (added > 0)
                    parts.push(qsTr("%n added", "", added));
                if (existing > 0)
                    parts.push(qsTr("%n already tracked", "", existing));
                if (invalid.length > 0)
                    parts.push(qsTr("%n not a valid address", "", invalid.length));
                addMany.result = parts.length > 0 ? parts.join(", ") : qsTr("Nothing to add.");
                // Only the entries that failed stay in the box, so they can be fixed.
                manyField.text = invalid.join("\n");
            }
        }

        contentItem: Column {
            spacing: Theme.spacingM

            Text {
                text: qsTr("Add many websites")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHeading + 2
                font.weight: Font.DemiBold
            }

            Text {
                width: parent.width
                text: qsTr("Paste or type addresses, one per line or separated by spaces or commas.")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
            }

            Rectangle {
                width: parent.width
                height: 200
                radius: Theme.radiusControl
                color: Theme.glassTint
                border.width: 1
                border.color: manyField.activeFocus ? Theme.withAlpha(Theme.accent, 0.9) : Theme.glassStroke

                ScrollView {
                    anchors.fill: parent

                    TextArea {
                        id: manyField

                        placeholderText: qsTr("example.com\nanother.com.tr\nhttps://third.org/")
                        placeholderTextColor: Theme.textMuted
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody + 1
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        background: null
                    }
                }
            }

            Row {
                spacing: Theme.spacingM

                ToggleSwitch {
                    id: analyzeToggle

                    checked: true
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Analyze them after adding (three at a time)")
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }
            }

            Text {
                width: parent.width
                visible: addMany.result.length > 0
                text: addMany.result
                color: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
            }

            Row {
                spacing: Theme.spacingS

                PrimaryButton {
                    implicitHeight: 40
                    implicitWidth: 140
                    text: qsTr("Add")
                    glyph: Icons.plus
                    enabled: manyField.text.trim().length > 0
                    onClicked: page.app.addSites(manyField.text, analyzeToggle.checked)
                }

                SecondaryButton {
                    implicitHeight: 40
                    text: qsTr("Close")
                    onClicked: addMany.close()
                }
            }
        }
    }

    // ---- remove selected ---------------------------------------------------------------
    Popup {
        id: removeConfirm

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 420
        modal: true
        padding: Theme.spacingXl
        closePolicy: Popup.CloseOnEscape

        background: Rectangle {
            radius: Theme.radiusCard
            color: Qt.rgba(0.07, 0.05, 0.12, 0.98)
            border.width: 1
            border.color: Theme.withAlpha(Theme.danger, 0.6)

            MouseArea {
                anchors.fill: parent
            }
        }

        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0.03, 0.55)
        }

        contentItem: Column {
            spacing: Theme.spacingM

            Text {
                text: qsTr("Remove %n website(s)?", "", page.app.sites.selectedCount)
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHeading + 2
                font.weight: Font.DemiBold
            }

            Text {
                width: parent.width
                text: qsTr("Their analyses, notes, to-dos, tags and screenshots are deleted too. This cannot be undone.")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
            }

            Row {
                spacing: Theme.spacingS

                PrimaryButton {
                    implicitHeight: 40
                    implicitWidth: 130
                    text: qsTr("Remove")
                    glyph: Icons.trash
                    onClicked: {
                        page.app.removeSites(page.app.sites.selectedIds);
                        removeConfirm.close();
                    }
                }

                SecondaryButton {
                    implicitHeight: 40
                    text: qsTr("Keep them")
                    onClicked: removeConfirm.close()
                }
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
