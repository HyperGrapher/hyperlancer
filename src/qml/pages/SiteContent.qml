import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hyperlancer

// The website's content as files: every page as Markdown plus its images and documents,
// saved in the app data folder by a content crawl. Shows the last crawl and its pages.
Item {
    id: panel

    required property AppContext app

    readonly property SiteDetail detail: app.siteDetail
    readonly property var crawl: detail.crawl
    readonly property bool hasCrawl: crawl.id !== undefined
    property int maxPages: 100
    // The page whose downloaded images are shown as a gallery; null shows the page list.
    property var galleryPage: null
    readonly property var galleryImages: galleryPage === null ? [] : detail.crawlImages.filter(image => image.pageIds.indexOf(galleryPage.id) >= 0)

    function resetScroll() {
        galleryPage = null;
        pageList.positionViewAtBeginning();
    }

    // A new crawl or another website replaces the pages the gallery was showing.
    Connections {
        target: panel.detail

        function onSiteIdChanged() {
            panel.galleryPage = null;
        }
    }

    GlassPanel {
        anchors.fill: parent
        visible: panel.galleryPage === null
        radius: Theme.radiusCard
        clip: true

        Item {
            id: bar

            width: parent.width
            height: 76

            Column {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingL + 2
                anchors.right: controls.left
                anchors.rightMargin: Theme.spacingL
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                Text {
                    width: parent.width
                    text: panel.detail.crawlRunning ? qsTr("Crawling…")
                          : panel.hasCrawl ? qsTr("%n page(s) saved", "", panel.crawl.pages) : qsTr("Not crawled yet")
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontHeading
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: panel.detail.crawlRunning ? (panel.detail.crawlLabel || qsTr("starting"))
                          : panel.hasCrawl ? [qsTr("%n image(s)", "", panel.crawl.images), qsTr("%n file(s)", "", panel.crawl.files),
                                              Format.bytes(panel.crawl.bytes),
                                              qsTr("crawled %1").arg(Format.relativeTime(panel.crawl.createdAt))].join("  ·  ")
                          : qsTr("Saves every page as Markdown with its images and documents, so you can read or reuse the content.")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    elide: Text.ElideRight
                }
            }

            Row {
                id: controls

                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingM
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingS

                Dropdown {
                    width: 160
                    visible: !panel.detail.crawlRunning
                    options: [50, 100, 200, 500].map(count => ({ value: count, label: qsTr("Up to %1 pages").arg(count) }))
                    currentValue: panel.maxPages
                    onActivated: value => panel.maxPages = value
                }

                SecondaryButton {
                    visible: panel.hasCrawl && !panel.detail.crawlRunning
                    text: qsTr("Open folder")
                    glyph: Icons.folderOpen
                    onClicked: panel.app.openLocalPath(panel.crawl.path)
                }

                PrimaryButton {
                    visible: !panel.detail.crawlRunning
                    implicitHeight: 40
                    implicitWidth: 150
                    text: panel.hasCrawl ? qsTr("Crawl again") : qsTr("Crawl website")
                    glyph: Icons.globe
                    enabled: panel.detail.site.url !== undefined
                    onClicked: panel.detail.startCrawl(panel.maxPages)
                }

                SecondaryButton {
                    visible: panel.detail.crawlRunning
                    text: qsTr("Cancel")
                    glyph: Icons.stop
                    onClicked: panel.detail.cancelCrawl()
                }
            }

            ThinProgress {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                visible: panel.detail.crawlRunning
                value: panel.detail.crawlFraction > 0 ? panel.detail.crawlFraction : -1
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.glassStroke
                visible: !panel.detail.crawlRunning
            }
        }

        Text {
            id: partialNote

            anchors.top: bar.bottom
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingL + 2
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingL
            anchors.topMargin: visible ? Theme.spacingS : 0
            height: visible ? implicitHeight : 0
            visible: panel.hasCrawl && !panel.detail.crawlRunning && (panel.crawl.status !== "done" || panel.crawl.errors > 0)
            text: panel.crawl.stopReason === "max_pages" ? qsTr("Stopped at the page limit. Crawl again with a higher limit to get more.")
                  : panel.crawl.stopReason === "size_cap" ? qsTr("Stopped at the size limit.")
                  : qsTr("%n request(s) failed during the crawl.", "", panel.crawl.errors || 0)
            color: Theme.warning
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WordWrap
        }

        ListView {
            id: pageList

            anchors.top: partialNote.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: panel.detail.crawlPages

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                id: row

                required property var modelData

                width: pageList.width
                height: 52

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSmall
                    color: rowArea.containsMouse ? Theme.rowHover : "transparent"
                }

                // The whole row opens the gallery of the page's images; the buttons on top keep their own clicks.
                MouseArea {
                    id: rowArea

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.galleryPage = row.modelData
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingM
                    anchors.right: words.left
                    anchors.rightMargin: Theme.spacingM
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Text {
                        width: parent.width
                        text: row.modelData.title || row.modelData.url
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody + 1
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }

                    Text {
                        width: parent.width
                        text: row.modelData.url
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption + 1
                        elide: Text.ElideMiddle
                        textFormat: Text.PlainText
                    }
                }

                Text {
                    id: words

                    anchors.right: openPage.left
                    anchors.rightMargin: Theme.spacingS
                    anchors.verticalCenter: parent.verticalCenter
                    width: 170
                    horizontalAlignment: Text.AlignRight
                    text: qsTr("%n word(s)", "", row.modelData.wordCount) + "  ·  " + qsTr("%n image(s)", "", row.modelData.imageCount)
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption + 1
                }

                IconButton {
                    id: openPage

                    anchors.right: openFile.left
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Icons.openExternal
                    flat: true
                    hint: qsTr("Open the page in the browser")
                    onClicked: panel.app.openExternal(row.modelData.url)
                }

                IconButton {
                    id: openFile

                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingS
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Icons.fileText
                    flat: true
                    hint: qsTr("Open the saved Markdown file")
                    onClicked: panel.app.openLocalPath(row.modelData.file)
                }

            }
        }

        EmptyState {
            anchors.centerIn: pageList
            width: pageList.width
            visible: panel.detail.crawlPages.length === 0 && !panel.detail.crawlRunning
            glyph: Icons.globe
            title: panel.hasCrawl ? qsTr("No pages saved") : qsTr("No content saved yet")
            text: panel.hasCrawl ? qsTr("The crawl did not find readable pages. Open the folder for its log.")
                                 : qsTr("Press Crawl website. Up to three analyses and one crawl run at a time.")
        }
    }
    // ---- the downloaded images of one page ----
    GlassPanel {
        anchors.fill: parent
        visible: panel.galleryPage !== null
        radius: Theme.radiusCard
        clip: true

        Item {
            id: galleryBar

            width: parent.width
            height: 64

            IconButton {
                id: backButton

                x: Theme.spacingM
                anchors.verticalCenter: parent.verticalCenter
                glyph: Icons.arrowLeft
                flat: true
                hint: qsTr("Back to the pages")
                onClicked: panel.galleryPage = null
            }

            Column {
                anchors.left: backButton.right
                anchors.leftMargin: Theme.spacingM
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingL
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                Text {
                    width: parent.width
                    text: panel.galleryPage ? (panel.galleryPage.title || panel.galleryPage.url) : ""
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontHeading
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }

                Text {
                    width: parent.width
                    text: qsTr("%n downloaded image(s). Click one to open it in the default app.", "", panel.galleryImages.length)
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    elide: Text.ElideRight
                }
            }
        }

        GridView {
            id: grid

            readonly property int columns: Math.max(1, Math.floor(width / 220))

            anchors.top: galleryBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            cellWidth: Math.floor(width / columns)
            cellHeight: cellWidth * 0.85 + 34
            model: panel.galleryImages

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                id: cell

                required property var modelData

                width: grid.cellWidth
                height: grid.cellHeight

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 6
                    radius: Theme.radiusSmall
                    color: cellArea.containsMouse ? Theme.rowHover : Theme.glassFill

                    Image {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: caption.top
                        anchors.margins: 6
                        source: cell.modelData.source
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        sourceSize.width: 360
                    }

                    Text {
                        id: caption

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 8
                        height: 20
                        text: (cell.modelData.width > 0 ? cell.modelData.width + " × " + cell.modelData.height + "  ·  " : "") + Format.bytes(cell.modelData.bytes)
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption + 1
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        id: cellArea

                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panel.app.openLocalPath(cell.modelData.file)
                    }
                }
            }
        }

        EmptyState {
            anchors.centerIn: grid
            width: grid.width
            visible: panel.galleryImages.length === 0
            glyph: Icons.article
            title: qsTr("No images on this page")
            text: qsTr("The crawl found no downloadable images here (they may be CSS backgrounds).")
        }
    }
}
