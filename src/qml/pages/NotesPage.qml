import QtQuick
import QtQuick.Controls
import Hyperlancer

// Every note of every website, under the website's domain. Click a note to read it; notes are
// edited on the website's own page (click the domain to go there).
PageBase {
    id: page

    required property AppContext app

    readonly property NoteList notes: app.notes
    property int expandedNote: -1

    signal siteOpened(var siteId)

    // Rows for the list: the website, then its notes (the list arrives ordered by website).
    readonly property var rows: {
        const result = [];
        let current = "";
        let heading = null;
        for (const note of notes.items) {
            if (note.siteKey !== current) {
                current = note.siteKey;
                heading = { kind: "heading", name: current, count: 0, siteId: note.siteId };
                result.push(heading);
            }
            heading.count += 1;
            result.push({ kind: "note", note: note });
        }
        return result;
    }

    onEntered: notes.refresh()

    PageHeader {
        id: header

        width: parent.width
        title: qsTr("Notes")
        subtitle: notes.items.length === 0 ? qsTr("No notes yet. Add notes on a website's Workspace tab.")
                  : qsTr("%n note(s) across all websites. Edit a note on its website's page.", "", notes.items.length)
        opacity: page.reveal(0)
        transform: Translate { y: page.revealShift(0) }
    }

    GlassPanel {
        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingXl
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        radius: Theme.radiusCard
        clip: true
        opacity: page.reveal(1)
        transform: Translate { y: page.revealShift(1) }

        ListView {
            id: list

            anchors.fill: parent
            anchors.margins: 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: page.rows

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                id: row

                required property var modelData
                readonly property bool isHeading: modelData.kind === "heading"
                readonly property var note: modelData.note
                readonly property bool expanded: !isHeading && page.expandedNote === note.id

                width: list.width
                height: isHeading ? 48 : noteCard.height + 4

                Text {
                    visible: row.isHeading
                    x: Theme.spacingL - 4
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    text: row.isHeading ? row.modelData.name + "  ·  " + row.modelData.count : ""
                    color: headingHover.containsMouse ? Theme.accent : Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontHeading
                    font.weight: Font.DemiBold

                    MouseArea {
                        id: headingHover

                        anchors.fill: parent
                        enabled: row.isHeading
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.siteOpened(row.modelData.siteId)
                    }
                }

                Rectangle {
                    id: noteCard

                    visible: !row.isHeading
                    x: Theme.spacingS
                    y: 2
                    width: parent.width - Theme.spacingS * 2
                    height: noteHeader.height + (row.expanded ? noteBody.implicitHeight + Theme.spacingM * 2 : 0)
                    radius: Theme.radiusSmall
                    color: cardHover.containsMouse ? Theme.rowHover : Theme.glassFill
                    clip: true

                    Behavior on height { NumberAnimation { duration: Motion.normal; easing.type: Easing.OutCubic } }

                    Item {
                        id: noteHeader

                        width: parent.width
                        height: 44

                        Icon {
                            id: chevron

                            x: Theme.spacingM
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: row.expanded ? Icons.caretDown : Icons.caretRight
                            size: 14
                            color: Theme.textMuted
                        }

                        Text {
                            anchors.left: chevron.right
                            anchors.leftMargin: Theme.spacingS
                            anchors.right: noteDate.left
                            anchors.rightMargin: Theme.spacingM
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.isHeading ? "" : (row.note.title.length > 0 ? row.note.title : qsTr("Untitled note"))
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody + 1
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }

                        Text {
                            id: noteDate

                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spacingL
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.isHeading ? "" : Format.shortDate(row.note.createdAt)
                            color: Theme.textMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontCaption + 1
                        }
                    }

                    Text {
                        id: noteBody

                        x: Theme.spacingM
                        anchors.top: noteHeader.bottom
                        width: parent.width - Theme.spacingM * 2
                        text: row.isHeading ? "" : row.note.body
                        textFormat: Text.MarkdownText
                        color: Theme.textPrimary
                        linkColor: Theme.accent
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody + 1
                        wrapMode: Text.Wrap
                        opacity: row.expanded ? 1 : 0
                        onLinkActivated: link => page.app.openExternal(link)

                        Behavior on opacity { NumberAnimation { duration: Motion.normal } }
                    }

                    // The header toggles; the link in the body keeps its own clicks.
                    MouseArea {
                        id: cardHover

                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: noteHeader.height
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.expandedNote = row.expanded ? -1 : row.note.id
                    }
                }
            }
        }

        EmptyState {
            anchors.centerIn: parent
            width: parent.width
            visible: page.rows.length === 0
            glyph: Icons.article
            title: qsTr("No notes yet")
            text: qsTr("Open a website's Workspace tab to write a note in Markdown.")
        }
    }
}
