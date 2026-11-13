import QtQuick

// Automated tour used for verification (--tour <dir>): visits every section, saves
// screenshots (also one mid-transition), then hammers the navigation with rapid switching
// and checks that exactly one page ends up fully visible. Prints a result line and quits.
Item {
    id: tour

    required property Window window
    required property string outputDirectory
    required property Item host
    property int sectionCount: 5

    signal navigateRequested(int index)
    // Adds a sample website with status, category, tags, to-do and note, and opens it.
    // Shows two sample mail notices; the capture goes to the notification window.
    signal sampleMailRequested
    signal mailPopupCaptureRequested(string path)
    signal settingsTabRequested(int tab)
    signal sampleSiteRequested
    // Shows a tab of the open website, or closes the detail view for -1.
    signal detailTabRequested(int tab)

    property int stepIndex: 0
    property int stressSwitches: 0
    readonly property int stressTotal: 40
    readonly property var stressPattern: [1, 3, 0, 2, 7, 1, 4, 0, 6, 5]

    readonly property var steps: [
        { wait: 0, run: () => navigateRequested(0) },
        { wait: 1600, run: () => capture("01-dashboard") },
        { wait: 0, run: () => navigateRequested(1) },
        { wait: 120, run: () => capture("02-transition-to-sites") },
        { wait: 1300, run: () => capture("03-sites") },
        { wait: 0, run: () => navigateRequested(2) },
        { wait: 1300, run: () => capture("04-emails") },
        { wait: 0, run: () => navigateRequested(3) },
        { wait: 1300, run: () => capture("05-todos") },
        { wait: 0, run: () => navigateRequested(4) },
        { wait: 1300, run: () => capture("06-notes") },
        { wait: 0, run: () => navigateRequested(5) },
        { wait: 1300, run: () => capture("06b-jobs") },
        { wait: 0, run: () => navigateRequested(6) },
        { wait: 1300, run: () => capture("06c-discover") },
        { wait: 0, run: () => navigateRequested(7) },
        { wait: 1300, run: () => capture("07-settings") },
        { wait: 0, run: () => settingsTabRequested(3) },
        { wait: 900, run: () => capture("07c-settings-appearance") },
        { wait: 400, run: () => sampleMailRequested() },
        { wait: 900, run: () => mailPopupCaptureRequested(outputDirectory + "/07b-mail-notification.png") },
        { wait: 700, run: () => navigateRequested(1) },
        { wait: 900, run: () => sampleSiteRequested() },
        { wait: 1800, run: () => detailTabRequested(0) },
        { wait: 1200, run: () => capture("08-site-profile") },
        { wait: 400, run: () => detailTabRequested(1) },
        { wait: 1000, run: () => capture("09-site-workspace") },
        { wait: 400, run: () => detailTabRequested(10) },
        { wait: 800, run: () => capture("09b-deadline-picker") },
        { wait: 400, run: () => detailTabRequested(11) },
        { wait: 800, run: () => capture("09c-tag-dropdown") },
        { wait: 400, run: () => detailTabRequested(12) },
        { wait: 900, run: () => capture("09d-note-markdown") },
        { wait: 400, run: () => detailTabRequested(5) },
        { wait: 900, run: () => capture("09e-screenshots") },
        { wait: 400, run: () => detailTabRequested(2) },
        { wait: 1000, run: () => capture("10-site-emails") },
        { wait: 400, run: () => detailTabRequested(-1) },
        { wait: 1200, run: () => capture("11-websites-with-data") },
        { wait: 400, run: () => startStress() }
    ]

    function capture(name) {
        const path = outputDirectory + "/" + name + ".png";
        window.contentItem.grabToImage(result => {
            if (result.saveToFile(path))
                console.log("TOUR saved", path);
            else
                console.warn("TOUR could not save", path);
        });
    }

    function runNextStep() {
        if (stepIndex >= steps.length)
            return;
        const step = steps[stepIndex];
        stepIndex += 1;
        stepTimer.interval = Math.max(1, step.wait);
        stepTimer.action = step.run;
        stepTimer.start();
    }

    function startStress() {
        stressTimer.start();
    }

    function finishStress() {
        const expected = stressPattern[(stressTotal - 1) % stressPattern.length];
        const presences = host.presences();
        let ok = host.currentIndex === expected;
        for (let i = 0; i < presences.length; ++i) {
            const target = i === expected ? 1 : 0;
            if (Math.abs(presences[i] - target) > 0.001)
                ok = false;
        }
        console.log("TOUR stress", ok ? "OK" : "FAILED", "current", host.currentIndex,
                    "expected", expected, "presences", JSON.stringify(presences));
        capture("12-after-rapid-switching");
        quitTimer.start();
    }

    Component.onCompleted: runNextStep()

    Timer {
        id: stepTimer

        property var action: null

        onTriggered: {
            action();
            tour.runNextStep();
        }
    }

    // Faster than any transition, so animations are always interrupted mid-way.
    Timer {
        id: stressTimer

        interval: 50
        repeat: true
        onTriggered: {
            tour.navigateRequested(tour.stressPattern[tour.stressSwitches % tour.stressPattern.length]);
            tour.stressSwitches += 1;
            if (tour.stressSwitches >= tour.stressTotal) {
                stop();
                settleTimer.start();
            }
        }
    }

    Timer {
        id: settleTimer

        interval: 1500
        onTriggered: tour.finishStress()
    }

    Timer {
        id: quitTimer

        interval: 800
        onTriggered: Qt.quit()
    }
}
