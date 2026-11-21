import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hyperlancer

// One visitor: their numbers, every session they had, and the chosen session's details and
// event timeline. The timeline updates live while the visitor is still on the site.
Item {
    id: view

    required property AppContext app

    readonly property VisitorStats visitors: app.visitors
    readonly property var detail: visitors.detail
    readonly property var visitor: detail.visitor || ({})
    readonly property var sessions: detail.sessions || []
    readonly property var current: {
        for (const session of sessions) {
            if (session._id === visitors.detailSessionId)
                return session;
        }
        return null;
    }
    readonly property real pageviews: sessions.reduce((sum, session) => sum + session.pageviews, 0)
    readonly property real activeMs: sessions.reduce((sum, session) => sum + (session.activeMs || 0), 0)

    function when(ms) {
        return ms ? Format.dateTime(ms) : "—";
    }

    function duration(ms) {
        const seconds = Math.round((ms || 0) / 1000);
        if (seconds < 60)
            return seconds + " s";
        const minutes = Math.floor(seconds / 60);
        return minutes < 60 ? minutes + " min " + (seconds % 60) + " s" : Math.floor(minutes / 60) + " h " + (minutes % 60) + " min";
    }

    function where(session) {
        return [session.city, session.region, session.country].filter(part => part && part.length > 0).join(", ") || qsTr("Unknown");
    }

    function props(event) {
        const values = event.props || ({});
        return Object.keys(values).map(key => key + "=" + values[key]).join("  ");
    }

    // The first session is shown when none was chosen.
    Connections {
        target: view.visitors

        function onDetailChanged() {
            if (view.visitors.detailVisitorId.length > 0 && view.visitors.detailSessionId.length === 0 && view.sessions.length > 0)
                view.visitors.selectSession(view.sessions[0]._id);
        }
    }

    // ---- top bar ----
    Item {
        id: bar

        width: parent.width
        height: 56

        IconButton {
            id: back

            anchors.verticalCenter: parent.verticalCenter
            glyph: Icons.arrowLeft
            implicitWidth: 40
            implicitHeight: 40
            hint: qsTr("Back to the list")
            onClicked: view.visitors.closeVisitor()
        }

        Column {
            anchors.left: back.right
            anchors.leftMargin: Theme.spacingM
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                width: parent.width
                text: qsTr("Visitor %1").arg((view.visitor.vid || "").slice(0, 8))
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontDisplay - 6
                font.weight: Font.Bold
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: view.current ? view.where(view.current) + "  ·  " + [view.current.deviceType, view.current.browser, view.current.os].filter(p => p).join(" · ") : ""
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                elide: Text.ElideRight
            }
        }
    }

    // ---- numbers ----
    Row {
        id: numbers

        anchors.top: bar.bottom
        anchors.topMargin: Theme.spacingM
        width: parent.width
        spacing: Theme.spacingM

        Repeater {
            model: [
                [qsTr("Sessions"), String(view.sessions.length)],
                [qsTr("Pageviews"), String(Math.round(view.pageviews))],
                [qsTr("Active time"), view.duration(view.activeMs)],
                [qsTr("First seen"), view.when(view.visitor.firstSeen)]
            ]

            delegate: GlassPanel {
                required property var modelData

                width: (numbers.width - 3 * numbers.spacing) / 4
                height: 70
                radius: Theme.radiusCard

                Column {
                    anchors.centerIn: parent
                    spacing: 4

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData[0].toUpperCase()
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1.1
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData[1]
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontHeading
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
    }

    // ---- sessions of this visitor ----
    GlassPanel {
        id: sessionsPanel

        anchors.top: numbers.bottom
        anchors.topMargin: Theme.spacingM
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: 290
        radius: Theme.radiusCard
        clip: true

        ListView {
            anchors.fill: parent
            anchors.margins: 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: view.sessions

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                id: sessionRow

                required property var modelData
                readonly property bool chosen: modelData._id === view.visitors.detailSessionId

                width: ListView.view.width
                height: 54

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: Theme.radiusSmall
                    color: sessionRow.chosen ? Theme.withAlpha(Theme.accent, 0.16) : sessionArea.containsMouse ? Theme.rowHover : "transparent"
                }

                MouseArea {
                    id: sessionArea

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: view.visitors.selectSession(sessionRow.modelData._id)
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingM
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingM
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Text {
                        width: parent.width
                        text: view.when(sessionRow.modelData.startedAt)
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        text: qsTr("%n page(s)", "", sessionRow.modelData.pageviews) + "  ·  " + view.duration(sessionRow.modelData.activeMs)
                              + (sessionRow.modelData.isBot ? "  ·  " + qsTr("bot") : "")
                        color: sessionRow.modelData.isBot ? Theme.danger : Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption + 1
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // ---- the chosen session ----
    GlassPanel {
        anchors.top: numbers.bottom
        anchors.topMargin: Theme.spacingM
        anchors.left: sessionsPanel.right
        anchors.leftMargin: Theme.spacingM
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        radius: Theme.radiusCard
        clip: true

        Flickable {
            id: scroller

            anchors.fill: parent
            anchors.margins: Theme.spacingL
            clip: true
            contentWidth: width
            contentHeight: body.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Column {
                id: body

                width: scroller.width
                spacing: Theme.spacingS
                visible: view.current !== null

                Repeater {
                    model: view.current ? [
                        [qsTr("Visit"), [
                            [qsTr("Started"), view.when(view.current.startedAt)],
                            [qsTr("Last activity"), view.when(view.current.lastActivityAt)],
                            [qsTr("Active time"), view.duration(view.current.activeMs)],
                            [qsTr("Pages"), String(view.current.pageviews)],
                            [qsTr("Site"), view.current.origin || ""],
                            [qsTr("Landing"), view.current.landingPath],
                            [qsTr("Exit"), view.current.exitPath],
                            [qsTr("Source"), view.current.referrerType + (view.current.referrer ? " · " + view.current.referrer : "")],
                            [qsTr("New visitor"), view.current.isNewVisitor ? qsTr("yes") : qsTr("no")]
                        ]],
                        [qsTr("Location"), [
                            [qsTr("Place"), view.where(view.current)],
                            [qsTr("Network"), view.current.isp || "—"],
                            [qsTr("IP"), view.current.ip || "—"],
                            [qsTr("Time zone"), view.current.timezone || "—"]
                        ]],
                        [qsTr("Device"), [
                            [qsTr("Type"), view.current.deviceType || "—"],
                            [qsTr("Browser"), (view.current.browser || "—") + (view.current.browserVersion ? " " + view.current.browserVersion : "")],
                            [qsTr("System"), (view.current.os || "—") + (view.current.osVersion ? " " + view.current.osVersion : "")],
                            [qsTr("Screen"), view.current.screenWidth ? view.current.screenWidth + " × " + view.current.screenHeight : "—"],
                            [qsTr("Connection"), view.current.connection || "—"]
                        ]],
                        [qsTr("Preferences"), [
                            [qsTr("Language"), (view.current.languages || []).join(", ") || view.current.language || "—"],
                            [qsTr("Colors"), view.current.colorScheme || "—"],
                            [qsTr("Reduced motion"), view.current.reducedMotion || "—"]
                        ]],
                        [qsTr("Bot check"), [
                            [qsTr("Verdict"), view.current.isBot ? qsTr("bot") : qsTr("person")],
                            [qsTr("Signals"), (view.current.botSignals || []).join(", ") || "—"]
                        ]]
                    ] : []

                    delegate: Column {
                        id: group

                        required property var modelData

                        width: body.width
                        spacing: 2

                        Text {
                            topPadding: Theme.spacingS
                            text: group.modelData[0].toUpperCase()
                            color: Theme.accent
                            font.family: Theme.fontFamily
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            font.letterSpacing: 1.1
                        }

                        Repeater {
                            model: group.modelData[1]

                            delegate: Row {
                                required property var modelData

                                width: group.width
                                spacing: Theme.spacingM

                                Text {
                                    width: 130
                                    text: modelData[0]
                                    color: Theme.textMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody
                                }

                                Text {
                                    width: parent.width - 130 - Theme.spacingM
                                    text: String(modelData[1])
                                    color: Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody
                                    wrapMode: Text.WrapAnywhere
                                    textFormat: Text.PlainText
                                }
                            }
                        }
                    }
                }

                Text {
                    topPadding: Theme.spacingL
                    text: qsTr("EVENTS  ·  %1").arg(view.visitors.detailEvents.length) + (view.visitors.detailEventsCapped ? qsTr("  (first ones only)") : "")
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.1
                }

                Repeater {
                    model: view.visitors.detailEvents

                    delegate: Row {
                        required property var modelData

                        width: body.width
                        spacing: Theme.spacingM

                        Text {
                            width: 70
                            text: new Date(modelData.ts).toLocaleTimeString(Qt.locale(), "HH:mm:ss")
                            color: Theme.textMuted
                            font.family: "monospace"
                            font.pixelSize: Theme.fontBody - 1
                        }

                        Text {
                            width: 90
                            text: modelData.type
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width - 160 - 2 * Theme.spacingM
                            text: [modelData.name, modelData.path, view.props(modelData)].filter(part => part && part.length > 0).join("  ·  ")
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            wrapMode: Text.WrapAnywhere
                            textFormat: Text.PlainText
                        }
                    }
                }
            }
        }

        EmptyState {
            anchors.centerIn: parent
            width: parent.width
            visible: view.current === null
            glyph: Icons.users
            title: view.sessions.length === 0 ? qsTr("Loading…") : qsTr("Pick a session")
            text: qsTr("Its details and events show here.")
        }
    }
}
