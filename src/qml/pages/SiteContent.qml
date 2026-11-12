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

    function resetScroll() {
        pageList.positionViewAtBeginning();
    }

    GlassPanel {
        anchors.fill: parent
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
                    width: 90
                    horizontalAlignment: Text.AlignRight
                    text: qsTr("%n word(s)", "", row.modelData.wordCount)
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

                MouseArea {
                    id: rowArea

                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
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
}
