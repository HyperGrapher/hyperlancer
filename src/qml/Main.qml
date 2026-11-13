import QtQuick
import Hyperlancer

// Root window: wires the window shell, ambient background, sidebar and pages together.
Window {
    id: root

    required property WindowChrome chrome
    required property AppSettings appSettings
    required property AppContext app
    required property bool reducedMotionForced
    required property bool hasTray
    required property string tourDirectory

    readonly property bool maximized: visibility === Window.Maximized || visibility === Window.FullScreen
    readonly property bool shown: visible && visibility !== Window.Minimized && visibility !== Window.Hidden
    // All decorative endless motion depends on this one flag.
    readonly property bool ambientActive: shown && appSettings.ambientEnabled && !Motion.reducedMotion
    readonly property real frameMargin: chrome.translucent && !maximized ? Theme.windowShadowMargin : 0
    readonly property real cornerRadius: chrome.translucent && !maximized ? Theme.radiusWindow : 0
    readonly property string databaseState: app.database.state === Database.Ready ? "ready"
                                            : app.database.state === Database.Failed ? "failed" : "connecting"

    readonly property int sectionDashboard: 0
    readonly property int sectionSites: 1
    readonly property int sectionDiscover: 2
    readonly property int sectionEmails: 3
    readonly property int sectionTodos: 4
    readonly property int sectionNotes: 5
    readonly property int sectionJobs: 6
    readonly property int sectionSettings: 7
    // Set once the user chose Quit: the window may then close for real.
    property bool quitting: false

    readonly property var sections: [
        { title: qsTr("Dashboard"), glyph: Icons.squaresFour },
        { title: qsTr("Websites"), glyph: Icons.globe },
        { title: qsTr("Discover"), glyph: Icons.binoculars },
        { title: qsTr("Emails"), glyph: Icons.envelope },
        { title: qsTr("To-dos"), glyph: Icons.checkSquare },
        { title: qsTr("Notes"), glyph: Icons.article },
        { title: qsTr("Jobs"), glyph: Icons.queue },
        { title: qsTr("Settings"), glyph: Icons.gearSix }
    ]

    // Pages ask for another page's content through these.
    signal siteRequested(var siteId)
    signal tourTabRequested(int tab)
    signal settingsTabRequested(int tab)

    // Switches section: moves the sidebar pill, spreads the new mood from the clicked icon,
    // re-tints the accent and transitions the page. Safe to call at any rate.
    function navigate(index, originInSidebar) {
        if (index === Theme.section)
            return;
        const origin = sidebar.mapToItem(background, originInSidebar.x, originInSidebar.y);
        background.spreadMood(Palettes.moods[index], origin);
        Theme.section = index;
        pageHost.show(index);
        appSettings.lastSection = index;
    }

    function openSite(siteId) {
        sidebar.select(sectionSites);
        siteRequested(siteId);
    }

    // From the tray icon: bring the window back.
    function showFromTray() {
        show();
        raise();
        requestActivate();
    }

    function openEmails() {
        showFromTray();
        sidebar.select(sectionEmails);
    }

    // From the tray menu. Running analyses still need the user's confirmation.
    function requestQuit() {
        quitting = true;
        if (app.jobRunner.runningCount > 0) {
            showFromTray();
            closeGuard.open();
            return;
        }
        Qt.quit();
    }

    function openSettings(tab) {
        sidebar.select(sectionSettings);
        settingsTabRequested(tab);
    }

    width: 1280
    height: 800
    minimumWidth: 1020
    minimumHeight: 660
    visible: true
    title: "Hyperlancer"
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinMaxButtonsHint
    color: chrome.translucent ? "transparent" : Palettes.moods[Theme.section].base

    onClosing: close => {
        // With a tray icon the app keeps running (checking mail, finishing analyses).
        if (hasTray && !quitting) {
            close.accepted = false;
            hide();
            return;
        }
        if (app.jobRunner.runningCount > 0 && !closeGuard.confirmed) {
            close.accepted = false;
            closeGuard.open();
        }
    }

    Binding {
        target: Motion
        property: "reducedMotion"
        value: root.reducedMotionForced || root.appSettings.reducedMotion
    }

    Binding {
        target: Theme
        property: "showFavicons"
        value: root.appSettings.showFavicons
    }

    Binding {
        target: Theme
        property: "glassHighlights"
        value: root.appSettings.glassHighlights
    }

    Component.onCompleted: {
        const section = appSettings.lastSection;
        Theme.section = section;
        background.setMood(Palettes.moods[section]);
        pageHost.showImmediately(section);
        if (!Motion.reducedMotion)
            introAnimation.start();
    }

    MailNotifications {
        id: mailNotifications

        app: root.app
        appSettings: root.appSettings
    }

    Connections {
        target: root.app

        function onOpenEmailsRequested() {
            root.openEmails();
        }

        function onNotice(message, isError) {
            toasts.show(message, isError);
        }
    }

    AmbientClock {
        id: ambientClock

        running: root.ambientActive
        speed: Theme.ambientSpeeds[root.appSettings.ambientSpeed]
        frameRate: Theme.ambientFrameRate
    }

    SoftShadow {
        anchors.fill: surface
        anchors.margins: -blur
        anchors.topMargin: -blur + 3
        anchors.bottomMargin: -blur - 5
        visible: root.frameMargin > 0
        radius: root.cornerRadius
        blur: root.frameMargin
        color: Qt.rgba(0, 0, 0, 0.5)
    }

    Item {
        id: surface

        anchors.fill: parent
        anchors.margins: root.frameMargin

        AuroraBackground {
            id: background

            anchors.fill: parent
            time: ambientClock.time
            intensity: root.appSettings.backgroundIntensity
            cornerRadius: root.cornerRadius
        }

        TitleBar {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            window: root
        }

        Sidebar {
            id: sidebar

            x: Theme.sidebarInset
            y: Theme.sidebarInset
            width: Theme.sidebarWidth
            height: parent.height - 2 * Theme.sidebarInset
            sections: root.sections
            currentIndex: Theme.section
            version: root.app.version
            badges: ({ [root.sectionEmails]: root.app.inbox.newCount })
            focus: true
            onActivated: (index, origin) => root.navigate(index, origin)
        }

        PageHost {
            id: pageHost

            anchors.left: sidebar.right
            anchors.leftMargin: 36
            anchors.right: parent.right
            anchors.rightMargin: 36
            anchors.top: parent.top
            anchors.topMargin: Theme.titleBarHeight + 10
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24

            pages: [
                Component {
                    DashboardPage {
                        app: root.app
                        ambientTime: ambientClock.time
                        onSiteOpened: siteId => root.openSite(siteId)
                        onSectionRequested: index => sidebar.select(index)
                    }
                },
                Component {
                    SitesPage {
                        id: sitesPage

                        app: root.app
                        ambientActive: root.ambientActive

                        Connections {
                            target: root

                            function onSiteRequested(siteId) {
                                sitesPage.openSite(siteId);
                            }

                            function onTourTabRequested(tab) {
                                sitesPage.showDetailTab(tab);
                            }
                        }
                    }
                },
                Component {
                    DiscoverPage {
                        app: root.app
                        onSettingsRequested: root.openSettings(0)
                    }
                },
                Component {
                    EmailsPage {
                        app: root.app
                    }
                },
                Component {
                    TodosPage {
                        app: root.app
                        onSiteOpened: siteId => root.openSite(siteId)
                    }
                },
                Component {
                    NotesPage {
                        app: root.app
                        onSiteOpened: siteId => root.openSite(siteId)
                    }
                },
                Component {
                    JobsPage {
                        app: root.app
                        ambientActive: root.ambientActive
                        onSiteOpened: siteId => root.openSite(siteId)
                    }
                },
                Component {
                    SettingsPage {
                        id: settingsPage

                        app: root.app
                        settings: root.appSettings

                        Connections {
                            target: root

                            function onSettingsTabRequested(tab) {
                                settingsPage.currentTab = tab;
                            }
                        }
                    }
                }
            ]
        }

        // Shown on every page while the database is unreachable: nothing else can work.
        Rectangle {
            id: databaseBanner

            anchors.horizontalCenter: pageHost.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 22
            width: Math.min(pageHost.width, bannerRow.implicitWidth + 32)
            height: 52
            radius: Theme.radiusControl
            color: Qt.rgba(0.08, 0.01, 0.03, 0.9)
            border.width: 1
            border.color: Theme.withAlpha(Theme.danger, 0.6)
            visible: opacity > 0
            opacity: root.databaseState === "failed" && Theme.section !== root.sectionSettings ? 1 : 0

            Behavior on opacity { NumberAnimation { duration: Motion.normal } }

            Row {
                id: bannerRow

                anchors.centerIn: parent
                spacing: Theme.spacingM

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Icons.database
                    color: Theme.danger
                    size: 18
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, pageHost.width - 220)
                    text: qsTr("Database unavailable: %1").arg(root.app.database.error)
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    elide: Text.ElideRight
                }

                SecondaryButton {
                    anchors.verticalCenter: parent.verticalCenter
                    implicitHeight: 32
                    text: qsTr("Database settings")
                    onClicked: root.openSettings(1)
                }
            }
        }

        Toast {
            id: toasts

            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24
        }

        PerformanceOverlay {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.top: parent.top
            anchors.topMargin: Theme.titleBarHeight
            visible: root.appSettings.performanceOverlay
            window: root
            ambientActive: root.ambientActive
        }

        // Asks before closing while analyses run, as the PRD requires.
        Rectangle {
            id: closeGuard

            property bool confirmed: false

            function open() {
                visible = true;
            }

            anchors.fill: parent
            radius: root.cornerRadius
            color: Qt.rgba(0, 0, 0.03, 0.55)
            visible: false

            TapHandler {}

            GlassPanel {
                anchors.centerIn: parent
                width: 420
                height: 190
                tintStrength: 1

                Column {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingXl
                    spacing: Theme.spacingM

                    Text {
                        text: qsTr("Analyses are still running")
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontHeading + 2
                        font.weight: Font.DemiBold
                    }

                    Text {
                        width: parent.width
                        text: qsTr("%n job(s) will be stopped. You can retry them from Jobs later.", "", root.app.jobRunner.runningCount)
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        spacing: Theme.spacingS
                        topPadding: Theme.spacingS

                        PrimaryButton {
                            text: qsTr("Stop and quit")
                            glyph: Icons.stop
                            onClicked: {
                                root.app.jobRunner.cancelAll();
                                closeGuard.confirmed = true;
                                Qt.quit();
                            }
                        }

                        SecondaryButton {
                            text: qsTr("Keep working")
                            onClicked: {
                                closeGuard.visible = false;
                                root.quitting = false;
                            }
                        }
                    }
                }
            }
        }

        ParallelAnimation {
            id: introAnimation

            NumberAnimation { target: surface; property: "opacity"; from: 0; to: 1; duration: Motion.windowIntro; easing.type: Easing.OutCubic }
            NumberAnimation { target: surface; property: "scale"; from: 0.985; to: 1; duration: Motion.windowIntro; easing.type: Easing.OutCubic }
        }
    }

    ResizeBorder {
        anchors.fill: parent
        window: root
        outside: root.frameMargin
        visible: !root.maximized
    }

    Repeater {
        model: root.sections.length

        delegate: Item {
            required property int index

            Shortcut {
                sequences: ["Ctrl+" + (index + 1)]
                onActivated: sidebar.select(index)
            }
        }
    }

    Loader {
        active: root.tourDirectory.length > 0
        sourceComponent: DemoTour {
            window: root
            outputDirectory: root.tourDirectory
            host: pageHost
            sectionCount: root.sections.length
            onNavigateRequested: index => sidebar.select(index)
            onSampleSiteRequested: root.app.addSite("example.org", false)
            onSampleMailRequested: mailNotifications.pending = [
                { gmailId: "a", from: "Ayşe Demir <ayse@firma.com.tr>", subject: "Re: Website offer", snippet: "" },
                { gmailId: "b", from: "info@baska.com", subject: "Meeting on Thursday?", snippet: "" }]
            onMailPopupCaptureRequested: path => mailNotifications.contentItem.grabToImage(result => result.saveToFile(path))
            onDetailTabRequested: tab => root.tourTabRequested(tab)
            onSettingsTabRequested: tab => root.settingsTabRequested(tab)
        }
    }
}
