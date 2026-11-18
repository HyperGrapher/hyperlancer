#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "AppSettings.h"

class AppSettingsTest final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void hasDefaults();
    void persistsValues();
    void clampsOutOfRangeValues();
    void emitsOnlyOnRealChanges();
    void resetRestoresDefaults();
    void persistsDatabaseAndToolSettings();
    void soundAssignmentsDefaultAndPersist();

private:
    [[nodiscard]] QString storagePath() const { return m_directory->filePath("settings.ini"); }

    std::unique_ptr<QTemporaryDir> m_directory;
};

void AppSettingsTest::init()
{
    m_directory = std::make_unique<QTemporaryDir>();
    QVERIFY(m_directory->isValid());
}

void AppSettingsTest::hasDefaults()
{
    const AppSettings settings(storagePath());
    QCOMPARE(settings.reducedMotion(), false);
    QCOMPARE(settings.ambientEnabled(), true);
    QCOMPARE(settings.glassHighlights(), true);
    QCOMPARE(settings.performanceOverlay(), false);
    QCOMPARE(settings.ambientSpeed(), AppSettings::AmbientSpeed::Normal);
    QCOMPARE(settings.backgroundIntensity(), 1.0);
    QCOMPARE(settings.lastSection(), 0);
}

void AppSettingsTest::persistsValues()
{
    {
        AppSettings settings(storagePath());
        settings.setReducedMotion(true);
        settings.setAmbientEnabled(false);
        settings.setAmbientSpeed(AppSettings::AmbientSpeed::Lively);
        settings.setBackgroundIntensity(0.5);
        settings.setLastSection(2);
    }

    const AppSettings reloaded(storagePath());
    QCOMPARE(reloaded.reducedMotion(), true);
    QCOMPARE(reloaded.ambientEnabled(), false);
    QCOMPARE(reloaded.ambientSpeed(), AppSettings::AmbientSpeed::Lively);
    QCOMPARE(reloaded.backgroundIntensity(), 0.5);
    QCOMPARE(reloaded.lastSection(), 2);
}

void AppSettingsTest::clampsOutOfRangeValues()
{
    AppSettings settings(storagePath());
    settings.setBackgroundIntensity(-3.0);
    QCOMPARE(settings.backgroundIntensity(), AppSettings::kMinimumIntensity);
    settings.setLastSection(99);
    QCOMPARE(settings.lastSection(), AppSettings::kSectionCount - 1);
}

void AppSettingsTest::emitsOnlyOnRealChanges()
{
    AppSettings settings(storagePath());
    QSignalSpy spy(&settings, &AppSettings::reducedMotionChanged);
    settings.setReducedMotion(false);
    QCOMPARE(spy.count(), 0);
    settings.setReducedMotion(true);
    settings.setReducedMotion(true);
    QCOMPARE(spy.count(), 1);
}

void AppSettingsTest::resetRestoresDefaults()
{
    AppSettings settings(storagePath());
    settings.setReducedMotion(true);
    settings.setPerformanceOverlay(true);
    settings.setBackgroundIntensity(0.3);
    settings.resetToDefaults();
    QCOMPARE(settings.reducedMotion(), false);
    QCOMPARE(settings.performanceOverlay(), false);
    QCOMPARE(settings.backgroundIntensity(), 1.0);
}

void AppSettingsTest::soundAssignmentsDefaultAndPersist()
{
    {
        AppSettings settings(storagePath());
        QVERIFY(settings.soundsEnabled());
        QCOMPARE(settings.soundFor(QStringLiteral("mailArrived")), QStringLiteral("message"));
        QCOMPARE(settings.soundFor(QStringLiteral("analysisFailed")), QStringLiteral("error"));
        QVERIFY(settings.soundFor(QStringLiteral("nonsense")).isEmpty());

        QSignalSpy changed(&settings, &AppSettings::soundAssignmentsChanged);
        settings.setSoundFor(QStringLiteral("mailArrived"), QStringLiteral("idea"));
        settings.setSoundFor(QStringLiteral("mailArrived"), QStringLiteral("idea")); // no change
        settings.setSoundFor(QStringLiteral("discoveryDone"), QString());            // deliberately silent
        settings.setSoundFor(QStringLiteral("nonsense"), QStringLiteral("idea"));    // unknown event
        QCOMPARE(changed.count(), 2);
        settings.setSoundVolume(7.0);
        QCOMPARE(settings.soundVolume(), 1.0);
    }
    // A new instance reads the same file: the choices, including "no sound", persist.
    const AppSettings reloaded(storagePath());
    QCOMPARE(reloaded.soundFor(QStringLiteral("mailArrived")), QStringLiteral("idea"));
    QVERIFY(reloaded.soundFor(QStringLiteral("discoveryDone")).isEmpty());
    QCOMPARE(reloaded.soundFor(QStringLiteral("analysisDone")), QStringLiteral("ding"));
    QCOMPARE(reloaded.soundAssignments().size(), 9);
    QCOMPARE(reloaded.soundVolume(), 1.0);
}

QTEST_GUILESS_MAIN(AppSettingsTest)
void AppSettingsTest::persistsDatabaseAndToolSettings()
{
    {
        AppSettings settings(storagePath());
        QCOMPARE(settings.databaseConfig().backend, DatabaseConfig::Backend::Sqlite);
        DatabaseConfig config;
        config.backend = DatabaseConfig::Backend::MySql;
        config.host = QStringLiteral(" db.example.com ");
        config.port = 3307;
        config.databaseName = QStringLiteral("leads");
        config.user = QStringLiteral("hl");
        config.password = QStringLiteral("must-not-be-stored");
        config.requireTls = false;
        settings.setDatabaseConfig(config);
        QSignalSpy spy(&settings, &AppSettings::toolPathsChanged);
        settings.setScriptsDirOverride(QStringLiteral("C:/tools/scripts"));
        QCOMPARE(spy.count(), 1);
    }
    AppSettings reloaded(storagePath());
    const DatabaseConfig config = reloaded.databaseConfig();
    QVERIFY(config.isMySql());
    QCOMPARE(config.host, QStringLiteral("db.example.com"));
    QCOMPARE(config.port, 3307);
    QCOMPARE(config.databaseName, QStringLiteral("leads"));
    QCOMPARE(config.requireTls, false);
    QVERIFY(config.password.isEmpty());
    QCOMPARE(reloaded.scriptsDirOverride(), QStringLiteral("C:/tools/scripts"));
    QFile file(storagePath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(!file.readAll().contains("must-not-be-stored"));
}

#include "tst_appsettings.moc"
