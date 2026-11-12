import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hyperlancer

// Settings in five tabs: external services and their keys, the database, the Python tools,
// appearance, and a health check of everything the app depends on.
PageBase {
    id: page

    required property AppContext app
    required property AppSettings settings
    property int currentTab: 0

    readonly property Integrations integrations: app.integrations
    readonly property GmailService gmail: app.gmail
    property var testResults: ({})

    function setTestResult(service, ok, message) {
        const results = Object.assign({}, testResults);
        results[service] = { ok: ok, message: message, pending: false };
        testResults = results;
    }

    function startTest(service) {
        const results = Object.assign({}, testResults);
        results[service] = { ok: false, message: qsTr("Testing…"), pending: true };
        testResults = results;
    }

    onEntered: app.health.refresh()

    Connections {
        target: page.integrations

        function onTestFinished(service, ok, message) {
            page.setTestResult(service, ok, message);
        }
    }

    PageHeader {
        id: header

        width: parent.width
        title: qsTr("Settings")
        subtitle: qsTr("Keys and passwords are stored in %1.").arg(page.integrations.secretStoreName)
        opacity: page.reveal(0)
        transform: Translate { y: page.revealShift(0) }

        Badge {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            visible: page.app.health.problemCount > 0
            text: qsTr("%n problem(s)", "", page.app.health.problemCount)
            tint: Theme.danger
            glyph: Icons.warningCircle
        }
    }

    SegmentedControl {
        id: tabs

        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingL + 2
        options: [qsTr("Connections"), qsTr("Database"), qsTr("Tools"), qsTr("Appearance"), qsTr("Health")]
        currentIndex: page.currentTab
        opacity: page.reveal(1)
        transform: Translate { y: page.revealShift(1) }
        onActivated: index => {
            page.currentTab = index;
            if (index === 4)
                page.app.health.refresh();
        }
    }

    Flickable {
        id: flick

        anchors.top: tabs.bottom
        anchors.topMargin: Theme.spacingL
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentHeight: stack.height + 12
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {
            policy: flick.contentHeight > flick.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        Item {
            id: stack

            width: flick.width
            height: [connectionsTab, databaseTab, toolsTab, appearanceTab, healthTab][page.currentTab].implicitHeight

            // ---- Connections -------------------------------------------------------------
            Column {
                id: connectionsTab

                width: parent.width
                spacing: Theme.spacingL
                visible: page.currentTab === 0
                opacity: page.reveal(2)

                Group {
                    title: qsTr("LANGSEARCH WEB SEARCH")

                    KeyRow {
                        service: "langsearch"
                        glyph: Icons.binoculars
                        title: qsTr("API key")
                        description: page.integrations.hasLangSearchKey
                                     ? qsTr("Saved: %1. Replace it whenever you rotate the key in the LangSearch dashboard.").arg(page.integrations.langSearchKeyHint)
                                     : qsTr("Used by Discover. Create a key at langsearch.com/dashboard.")
                        hasKey: page.integrations.hasLangSearchKey
                        onSaveRequested: key => page.integrations.setLangSearchKey(key)
                        onRemoveRequested: page.integrations.removeLangSearchKey()
                        onTestRequested: {
                            page.startTest("langsearch");
                            page.integrations.testLangSearch();
                        }
                    }
                }

                Group {
                    title: qsTr("PAGESPEED INSIGHTS")

                    KeyRow {
                        service: "pagespeed"
                        glyph: Icons.deviceMobile
                        title: qsTr("API key (optional)")
                        description: page.integrations.hasPageSpeedKey
                                     ? qsTr("Saved: %1.").arg(page.integrations.pageSpeedKeyHint)
                                     : qsTr("An API key (starts with AIza), not the OAuth client secret. Google Cloud → APIs & Services → Credentials → Create credentials → API key. Without one, speed checks use a small shared quota.")
                        hasKey: page.integrations.hasPageSpeedKey
                        onSaveRequested: key => page.integrations.setPageSpeedKey(key)
                        onRemoveRequested: page.integrations.removePageSpeedKey()
                        onTestRequested: {
                            page.startTest("pagespeed");
                            page.integrations.testPageSpeed();
                        }
                    }
                }

                Group {
                    title: qsTr("GMAIL (READ-ONLY)")

                    SettingRow {
                        width: parent.width
                        glyph: Icons.key
                        title: qsTr("OAuth client")
                        description: page.gmail.clientConfigured
                                     ? qsTr("Imported (%1). Stored in %2; the JSON file is no longer needed.").arg(page.gmail.clientIdHint).arg(page.integrations.secretStoreName)
                                     : qsTr("Import the client_secret_….json file of your Desktop OAuth client.")

                        Row {
                            spacing: Theme.spacingS

                            SecondaryButton {
                                text: page.gmail.clientConfigured ? qsTr("Replace…") : qsTr("Import file…")
                                glyph: Icons.folderOpen
                                onClicked: clientFileDialog.open()
                            }

                            IconButton {
                                visible: page.gmail.clientConfigured
                                glyph: Icons.trash
                                danger: true
                                hint: qsTr("Forget the client (double-click)")
                                onDoubleClicked: page.gmail.removeClient()
                            }
                        }
                    }

                    SettingRow {
                        width: parent.width
                        glyph: Icons.googleLogo
                        title: page.gmail.connected ? page.gmail.email : qsTr("Mailbox")
                        description: page.gmail.status

                        Row {
                            spacing: Theme.spacingS

                            ThinProgress {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 60
                                value: -1
                                visible: page.gmail.busy
                            }

                            PrimaryButton {
                                visible: !page.gmail.connected
                                implicitHeight: 40
                                implicitWidth: 130
                                text: qsTr("Connect")
                                glyph: Icons.plugsConnected
                                enabled: page.gmail.clientConfigured && !page.gmail.busy
                                onClicked: page.gmail.connectAccount()
                            }

                            SecondaryButton {
                                visible: page.gmail.connected
                                text: qsTr("Disconnect")
                                glyph: Icons.signOut
                                onClicked: page.gmail.disconnectAccount()
                            }
                        }
                    }
                }
            }

            // ---- Database ----------------------------------------------------------------
            Column {
                id: databaseTab

                property bool useMySql: false
                property string host: ""
                property string port: "3306"
                property string databaseName: ""
                property string user: ""
                property string password: ""
                property bool requireTls: true
                property bool hasStoredPassword: false

                function load() {
                    const s = page.integrations.databaseSettings;
                    useMySql = s.backend === "mysql";
                    host = s.host;
                    port = String(s.port);
                    databaseName = s.database;
                    user = s.user;
                    requireTls = s.requireTls;
                    hasStoredPassword = s.hasPassword;
                    password = "";
                }

                function values() {
                    return { backend: useMySql ? "mysql" : "sqlite", host: host, port: parseInt(port) || 3306,
                             database: databaseName, user: user, password: password, requireTls: requireTls };
                }

                width: parent.width
                spacing: Theme.spacingL
                visible: page.currentTab === 1
                Component.onCompleted: load()

                Group {
                    title: qsTr("CURRENT CONNECTION")

                    SettingRow {
                        width: parent.width
                        glyph: Icons.database
                        title: page.app.database.description
                        description: page.app.database.state === Database.Ready ? qsTr("Connected · schema version %1").arg(page.app.database.schemaVersion)
                                     : page.app.database.state === Database.Failed ? page.app.database.error : qsTr("Connecting…")

                        StatusDot {
                            size: 10
                            color: page.app.database.state === Database.Ready ? Theme.success
                                   : page.app.database.state === Database.Failed ? Theme.danger : Theme.warning
                            active: page.app.database.state === Database.Connecting
                        }
                    }
                }

                Group {
                    title: qsTr("WHERE RECORDS ARE STORED")

                    SettingRow {
                        width: parent.width
                        glyph: Icons.hardDrives
                        title: qsTr("Database")
                        description: databaseTab.useMySql ? qsTr("Remote MySQL or MariaDB server, shared across machines.")
                                                          : qsTr("A local SQLite file in the data folder. No setup needed.")

                        SegmentedControl {
                            options: [qsTr("SQLite (local)"), qsTr("MySQL (remote)")]
                            currentIndex: databaseTab.useMySql ? 1 : 0
                            onActivated: index => databaseTab.useMySql = index === 1
                        }
                    }

                    Column {
                        width: parent.width
                        spacing: Theme.spacingM
                        visible: databaseTab.useMySql
                        topPadding: Theme.spacingS
                        bottomPadding: Theme.spacingM

                        Text {
                            width: parent.width
                            visible: !page.integrations.mySqlDriverAvailable
                            text: qsTr("The Qt MySQL driver (qsqlmysql) is not installed next to the app, so a MySQL connection cannot open yet. See the README section “MySQL driver on Windows”.")
                            color: Theme.warning
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            wrapMode: Text.WordWrap
                        }

                        GridLayout {
                            width: parent.width
                            columns: 4
                            columnSpacing: Theme.spacingM
                            rowSpacing: Theme.spacingM

                            FieldLabel { text: qsTr("Host") }
                            StyledTextField {
                                Layout.fillWidth: true
                                placeholderText: "db.example.com"
                                text: databaseTab.host
                                onTextEdited: databaseTab.host = text
                            }
                            FieldLabel { text: qsTr("Port") }
                            StyledTextField {
                                Layout.preferredWidth: 110
                                text: databaseTab.port
                                validator: IntValidator { bottom: 1; top: 65535 }
                                onTextEdited: databaseTab.port = text
                            }

                            FieldLabel { text: qsTr("Database") }
                            StyledTextField {
                                Layout.fillWidth: true
                                placeholderText: "hyperlancer"
                                text: databaseTab.databaseName
                                onTextEdited: databaseTab.databaseName = text
                            }
                            FieldLabel { text: qsTr("TLS") }
                            Row {
                                spacing: Theme.spacingS

                                ToggleSwitch {
                                    checked: databaseTab.requireTls
                                    onToggled: databaseTab.requireTls = checked
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: databaseTab.requireTls ? qsTr("Required") : qsTr("Off")
                                    color: databaseTab.requireTls ? Theme.textSecondary : Theme.warning
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody
                                }
                            }

                            FieldLabel { text: qsTr("User") }
                            StyledTextField {
                                Layout.fillWidth: true
                                text: databaseTab.user
                                onTextEdited: databaseTab.user = text
                            }
                            FieldLabel { text: qsTr("Password") }
                            StyledTextField {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 180
                                echoMode: TextInput.Password
                                placeholderText: databaseTab.hasStoredPassword ? qsTr("saved — leave empty to keep") : ""
                                text: databaseTab.password
                                onTextEdited: databaseTab.password = text
                            }
                        }
                    }

                    RowLayout {
                        width: parent.width
                        spacing: Theme.spacingS

                        TestResultText {
                            Layout.fillWidth: true
                            result: page.testResults.database
                        }

                        SecondaryButton {
                            text: qsTr("Test connection")
                            glyph: Icons.plug
                            onClicked: {
                                page.startTest("database");
                                page.integrations.testDatabase(databaseTab.values());
                            }
                        }

                        PrimaryButton {
                            implicitHeight: 40
                            implicitWidth: 170
                            text: qsTr("Save and restart")
                            glyph: Icons.refresh
                            onClicked: page.integrations.applyDatabase(databaseTab.values())
                        }
                    }

                    Item { width: 1; height: Theme.spacingS }
                }

                Text {
                    width: parent.width
                    text: qsTr("Switching databases does not copy records. Tables are created automatically on the first connection. Use a dedicated MySQL user with rights on one schema only.")
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption + 1
                    wrapMode: Text.WordWrap
                }
            }

            // ---- Tools ------------------------------------------------------------------
            Column {
                id: toolsTab

                width: parent.width
                spacing: Theme.spacingL
                visible: page.currentTab === 2

                Group {
                    title: qsTr("PYTHON TOOLS")

                    SettingRow {
                        width: parent.width
                        glyph: Icons.folderOpen
                        title: qsTr("Tools folder")
                        description: page.integrations.scriptsDir.length > 0 ? page.integrations.scriptsDir
                                     : qsTr("Not found. Pick the web-dashboard\\scripts folder.")

                        Row {
                            spacing: Theme.spacingS

                            SecondaryButton {
                                text: qsTr("Choose…")
                                onClicked: scriptsDialog.open()
                            }

                            SecondaryButton {
                                text: qsTr("Auto")
                                enabled: page.settings.scriptsDirOverride.length > 0
                                onClicked: page.integrations.setScriptsDir("")
                            }
                        }
                    }

                    SettingRow {
                        width: parent.width
                        glyph: Icons.terminal
                        title: qsTr("Python")
                        description: page.integrations.pythonPath.length > 0 ? page.integrations.pythonPath : qsTr("Not found")

                        Row {
                            spacing: Theme.spacingS

                            SecondaryButton {
                                text: qsTr("Choose…")
                                onClicked: pythonDialog.open()
                            }

                            SecondaryButton {
                                text: qsTr("Auto")
                                enabled: page.settings.pythonOverride.length > 0
                                onClicked: page.integrations.setPythonPath("")
                            }
                        }
                    }
                }

                Group {
                    title: qsTr("DATA")

                    SettingRow {
                        width: parent.width
                        glyph: Icons.hardDrives
                        title: qsTr("Data folder")
                        description: page.integrations.dataDir

                        SecondaryButton {
                            text: qsTr("Open")
                            glyph: Icons.openExternal
                            onClicked: page.app.openDataFolder()
                        }
                    }
                }
            }

            // ---- Appearance --------------------------------------------------------------
            Column {
                id: appearanceTab

                width: parent.width
                spacing: Theme.spacingL
                visible: page.currentTab === 3

                Group {
                    title: qsTr("MOTION")

                    SettingRow {
                        width: parent.width
                        glyph: Icons.moon
                        title: qsTr("Reduced motion")
                        description: qsTr("Static background, quick cross-fades, no stagger or tilt.")

                        ToggleSwitch {
                            checked: page.settings.reducedMotion
                            onToggled: page.settings.reducedMotion = checked
                        }
                    }

                    SettingRow {
                        width: parent.width
                        glyph: Icons.drop
                        title: qsTr("Ambient background")
                        description: qsTr("Slowly flowing color behind the interface.")

                        ToggleSwitch {
                            checked: page.settings.ambientEnabled
                            onToggled: page.settings.ambientEnabled = checked
                        }
                    }

                    SettingRow {
                        width: parent.width
                        glyph: Icons.waveform
                        title: qsTr("Ambient speed")
                        description: qsTr("How fast the background drifts.")
                        enabled: page.settings.ambientEnabled && !page.settings.reducedMotion
                        opacity: enabled ? 1 : 0.45

                        SegmentedControl {
                            options: [qsTr("Calm"), qsTr("Normal"), qsTr("Lively")]
                            currentIndex: page.settings.ambientSpeed
                            onActivated: index => page.settings.ambientSpeed = index
                        }
                    }
                }

                Group {
                    title: qsTr("APPEARANCE")

                    SettingRow {
                        width: parent.width
                        glyph: Icons.palette
                        title: qsTr("Background intensity")
                        description: qsTr("Strength of the colors over the base.")

                        StyledSlider {
                            from: 0.2
                            to: 1.0
                            value: page.settings.backgroundIntensity
                            onMoved: page.settings.backgroundIntensity = value
                        }
                    }

                    SettingRow {
                        width: parent.width
                        glyph: Icons.sparkle
                        title: qsTr("Glass highlights")
                        description: qsTr("Cards tilt toward the pointer and catch its light.")

                        ToggleSwitch {
                            checked: page.settings.glassHighlights
                            onToggled: page.settings.glassHighlights = checked
                        }
                    }

                    SettingRow {
                        width: parent.width
                        glyph: Icons.gauge
                        title: qsTr("Performance overlay")
                        description: qsTr("Frame rate and frame times, for checking rendering cost.")

                        ToggleSwitch {
                            checked: page.settings.performanceOverlay
                            onToggled: page.settings.performanceOverlay = checked
                        }
                    }
                }
            }

            // ---- Health ------------------------------------------------------------------
            Column {
                id: healthTab

                width: parent.width
                spacing: Theme.spacingL
                visible: page.currentTab === 4

                Group {
                    title: qsTr("ENVIRONMENT")

                    Repeater {
                        model: page.app.health.checks

                        delegate: Item {
                            id: check

                            required property var modelData
                            required property int index

                            width: parent.width
                            implicitHeight: Math.max(58, textColumn.implicitHeight + 20)

                            Rectangle {
                                id: stateBadge

                                anchors.verticalCenter: parent.verticalCenter
                                width: 36
                                height: 36
                                radius: 10
                                color: Theme.withAlpha(Theme.statusColor(check.modelData.state), 0.14)

                                Icon {
                                    anchors.centerIn: parent
                                    glyph: check.modelData.state === "ok" ? Icons.checkCircle
                                           : check.modelData.state === "error" ? Icons.xCircle
                                           : check.modelData.state === "checking" ? Icons.hourglass : Icons.warning
                                    filled: true
                                    size: 18
                                    color: Theme.statusColor(check.modelData.state)
                                }
                            }

                            Column {
                                id: textColumn

                                anchors.left: stateBadge.right
                                anchors.leftMargin: Theme.spacingM + 2
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2

                                Text {
                                    text: check.modelData.title
                                    color: Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody + 1
                                    font.weight: Font.Medium
                                }

                                Text {
                                    width: parent.width
                                    text: check.modelData.detail
                                    color: Theme.textSecondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontCaption + 1
                                    wrapMode: Text.WrapAnywhere
                                    maximumLineCount: 3
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: parent.width
                                    visible: check.modelData.hint.length > 0
                                    text: check.modelData.hint
                                    color: Theme.withAlpha(Theme.statusColor(check.modelData.state), 0.95)
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontCaption + 1
                                    wrapMode: Text.WordWrap
                                }
                            }

                            Rectangle {
                                anchors.left: textColumn.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: Theme.rowDivider
                                visible: check.index < page.app.health.checks.length - 1
                            }
                        }
                    }
                }

                Row {
                    spacing: Theme.spacingS

                    SecondaryButton {
                        text: qsTr("Check again")
                        glyph: Icons.refresh
                        onClicked: page.app.health.refresh()
                    }

                    SecondaryButton {
                        text: qsTr("Open data folder")
                        glyph: Icons.folderOpen
                        onClicked: page.app.openDataFolder()
                    }
                }

                Text {
                    text: qsTr("Hyperlancer %1").arg(page.app.version)
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                }
            }
        }
    }

    FileDialog {
        id: clientFileDialog

        title: qsTr("Choose the Google OAuth client file")
        nameFilters: [qsTr("JSON files (*.json)")]
        onAccepted: page.gmail.importClientFile(selectedFile.toString())
    }

    FolderDialog {
        id: scriptsDialog

        title: qsTr("Choose the Python tools folder (web-dashboard/scripts)")
        onAccepted: page.integrations.setScriptsDir(selectedFolder.toString())
    }

    FileDialog {
        id: pythonDialog

        title: qsTr("Choose the Python interpreter")
        onAccepted: page.integrations.setPythonPath(selectedFile.toString())
    }

    // Titled glass group, the same look as the template's preference groups.
    component Group: GlassPanel {
        id: group

        property string title
        default property alias rows: rowColumn.data

        width: parent.width
        height: rowColumn.implicitHeight + 52
        radius: Theme.radiusCard

        Text {
            x: Theme.spacingXl
            y: Theme.spacingL
            text: group.title
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: 10
            font.weight: Font.DemiBold
            font.letterSpacing: 1.4
        }

        Column {
            id: rowColumn

            x: Theme.spacingXl
            y: 36
            width: parent.width - 2 * Theme.spacingXl
        }
    }

    component FieldLabel: Text {
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
    }

    component TestResultText: Text {
        property var result: undefined

        visible: result !== undefined
        text: result ? result.message : ""
        color: !result || result.pending ? Theme.textSecondary : result.ok ? Theme.success : Theme.danger
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        wrapMode: Text.WordWrap
        maximumLineCount: 3
        elide: Text.ElideRight
    }

    // An API key: shows whether one is saved, and lets the user add, replace (rotate),
    // test or remove it. The key itself is never shown again after saving.
    component KeyRow: Column {
        id: keyRow

        property string service
        property string glyph
        property string title
        property string description
        property bool hasKey: false
        property bool editing: false

        signal saveRequested(string key)
        signal removeRequested
        signal testRequested

        width: parent.width
        bottomPadding: Theme.spacingS

        SettingRow {
            width: parent.width
            glyph: keyRow.glyph
            title: keyRow.title
            description: keyRow.description

            Row {
                spacing: Theme.spacingS

                Badge {
                    anchors.verticalCenter: parent.verticalCenter
                    text: keyRow.hasKey ? qsTr("Saved") : qsTr("Not set")
                    tint: keyRow.hasKey ? Theme.success : Theme.textMuted
                }

                SecondaryButton {
                    text: keyRow.hasKey ? qsTr("Replace") : qsTr("Add key")
                    glyph: Icons.key
                    onClicked: {
                        keyRow.editing = !keyRow.editing;
                        if (keyRow.editing)
                            keyField.forceActiveFocus();
                    }
                }

                SecondaryButton {
                    text: qsTr("Test")
                    enabled: keyRow.hasKey || keyRow.service === "pagespeed"
                    onClicked: keyRow.testRequested()
                }

                IconButton {
                    visible: keyRow.hasKey
                    glyph: Icons.trash
                    danger: true
                    hint: qsTr("Remove key (double-click)")
                    onDoubleClicked: keyRow.removeRequested()
                }
            }
        }

        RowLayout {
            width: parent.width
            spacing: Theme.spacingS
            visible: keyRow.editing
            height: visible ? implicitHeight : 0

            StyledTextField {
                id: keyField

                Layout.fillWidth: true
                Layout.leftMargin: 50
                echoMode: TextInput.Password
                placeholderText: qsTr("Paste the new key")
                onAccepted: saveButton.clicked()
            }

            PrimaryButton {
                id: saveButton

                implicitHeight: 40
                implicitWidth: 110
                text: qsTr("Save")
                enabled: keyField.text.trim().length > 0
                onClicked: {
                    keyRow.saveRequested(keyField.text.trim());
                    keyField.clear();
                    keyRow.editing = false;
                }
            }

            SecondaryButton {
                text: qsTr("Cancel")
                onClicked: {
                    keyField.clear();
                    keyRow.editing = false;
                }
            }
        }

        TestResultText {
            x: 50
            width: parent.width - 50
            result: page.testResults[keyRow.service]
        }
    }
}
