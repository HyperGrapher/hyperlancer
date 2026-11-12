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

    property int stepIndex: 0
    property int stressSwitches: 0
    readonly property int stressTotal: 40
    readonly property var stressPattern: [1, 3, 0, 2, 4, 1, 2, 0]

    readonly property var steps: [
        { wait: 0, run: () => navigateRequested(0) },
        { wait: 1600, run: () => capture("01-dashboard") },
        { wait: 0, run: () => navigateRequested(1) },
        { wait: 120, run: () => capture("02-transition-to-sites") },
        { wait: 1300, run: () => capture("03-sites") },
        { wait: 0, run: () => navigateRequested(2) },
        { wait: 1300, run: () => capture("04-discover") },
        { wait: 0, run: () => navigateRequested(3) },
        { wait: 1300, run: () => capture("05-jobs") },
        { wait: 0, run: () => navigateRequested(4) },
        { wait: 1300, run: () => capture("06-settings") },
        { wait: 0, run: () => startStress() }
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
        capture("07-after-rapid-switching");
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
