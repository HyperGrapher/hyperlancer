#include "AppSettings.h"

#include <algorithm>

namespace {

constexpr auto kReducedMotionKey = "motion/reduced";
constexpr auto kAmbientEnabledKey = "motion/ambient";
constexpr auto kAmbientSpeedKey = "motion/ambientSpeed";
constexpr auto kGlassHighlightsKey = "appearance/glassHighlights";
constexpr auto kGmailChromeProfileKey = "gmail/chromeProfile";
constexpr auto kNotifyNewMailKey = "gmail/notifyNewMail";
constexpr auto kShowFaviconsKey = "appearance/showFavicons";
constexpr auto kBackgroundIntensityKey = "appearance/backgroundIntensity";
constexpr auto kPerformanceOverlayKey = "diagnostics/performanceOverlay";
constexpr auto kLastSectionKey = "navigation/lastSection";
constexpr auto kScriptsDirKey = "tools/scriptsDir";
constexpr auto kPythonKey = "tools/python";
constexpr auto kDbBackendKey = "database/backend";
constexpr auto kDbHostKey = "database/host";
constexpr auto kDbPortKey = "database/port";
constexpr auto kDbNameKey = "database/name";
constexpr auto kDbUserKey = "database/user";
constexpr auto kDbTlsKey = "database/requireTls";

AppSettings::AmbientSpeed toAmbientSpeed(int value)
{
    const int clamped = std::clamp(value, static_cast<int>(AppSettings::AmbientSpeed::Calm),
                                   static_cast<int>(AppSettings::AmbientSpeed::Lively));
    return static_cast<AppSettings::AmbientSpeed>(clamped);
}

} // namespace

AppSettings::AppSettings(const QString &storagePath, QObject *parent)
    : QObject(parent)
    , m_storage(storagePath, QSettings::IniFormat)
{
    m_reducedMotion = m_storage.value(kReducedMotionKey, m_reducedMotion).toBool();
    m_ambientEnabled = m_storage.value(kAmbientEnabledKey, m_ambientEnabled).toBool();
    m_glassHighlights = m_storage.value(kGlassHighlightsKey, m_glassHighlights).toBool();
    m_gmailChromeProfile = m_storage.value(kGmailChromeProfileKey).toString();
    m_notifyNewMail = m_storage.value(kNotifyNewMailKey, m_notifyNewMail).toBool();
    m_showFavicons = m_storage.value(kShowFaviconsKey, m_showFavicons).toBool();
    m_performanceOverlay = m_storage.value(kPerformanceOverlayKey, m_performanceOverlay).toBool();
    m_ambientSpeed =
        toAmbientSpeed(m_storage.value(kAmbientSpeedKey, static_cast<int>(m_ambientSpeed)).toInt());
    m_backgroundIntensity =
        std::clamp(m_storage.value(kBackgroundIntensityKey, m_backgroundIntensity).toDouble(),
                   kMinimumIntensity, 1.0);
    m_lastSection =
        std::clamp(m_storage.value(kLastSectionKey, m_lastSection).toInt(), 0, kSectionCount - 1);
    m_scriptsDirOverride = m_storage.value(kScriptsDirKey).toString();
    m_pythonOverride = m_storage.value(kPythonKey).toString();
}

void AppSettings::setReducedMotion(bool enabled)
{
    if (m_reducedMotion == enabled) {
        return;
    }
    m_reducedMotion = enabled;
    m_storage.setValue(kReducedMotionKey, enabled);
    emit reducedMotionChanged();
}

void AppSettings::setAmbientEnabled(bool enabled)
{
    if (m_ambientEnabled == enabled) {
        return;
    }
    m_ambientEnabled = enabled;
    m_storage.setValue(kAmbientEnabledKey, enabled);
    emit ambientEnabledChanged();
}

void AppSettings::setGmailChromeProfile(const QString &profileName)
{
    const QString trimmed = profileName.trimmed();
    if (m_gmailChromeProfile == trimmed) {
        return;
    }
    m_gmailChromeProfile = trimmed;
    m_storage.setValue(kGmailChromeProfileKey, trimmed);
    emit gmailChromeProfileChanged();
}

void AppSettings::setNotifyNewMail(bool enabled)
{
    if (m_notifyNewMail == enabled) {
        return;
    }
    m_notifyNewMail = enabled;
    m_storage.setValue(kNotifyNewMailKey, enabled);
    emit notifyNewMailChanged();
}

void AppSettings::setShowFavicons(bool enabled)
{
    if (m_showFavicons == enabled) {
        return;
    }
    m_showFavicons = enabled;
    m_storage.setValue(kShowFaviconsKey, enabled);
    emit showFaviconsChanged();
}

void AppSettings::setGlassHighlights(bool enabled)
{
    if (m_glassHighlights == enabled) {
        return;
    }
    m_glassHighlights = enabled;
    m_storage.setValue(kGlassHighlightsKey, enabled);
    emit glassHighlightsChanged();
}

void AppSettings::setPerformanceOverlay(bool visible)
{
    if (m_performanceOverlay == visible) {
        return;
    }
    m_performanceOverlay = visible;
    m_storage.setValue(kPerformanceOverlayKey, visible);
    emit performanceOverlayChanged();
}

void AppSettings::setAmbientSpeed(AmbientSpeed speed)
{
    if (m_ambientSpeed == speed) {
        return;
    }
    m_ambientSpeed = speed;
    m_storage.setValue(kAmbientSpeedKey, static_cast<int>(speed));
    emit ambientSpeedChanged();
}

void AppSettings::setBackgroundIntensity(double intensity)
{
    const double clamped = std::clamp(intensity, kMinimumIntensity, 1.0);
    if (qFuzzyCompare(m_backgroundIntensity, clamped)) {
        return;
    }
    m_backgroundIntensity = clamped;
    m_storage.setValue(kBackgroundIntensityKey, clamped);
    emit backgroundIntensityChanged();
}

void AppSettings::setLastSection(int section)
{
    const int clamped = std::clamp(section, 0, kSectionCount - 1);
    if (m_lastSection == clamped) {
        return;
    }
    m_lastSection = clamped;
    m_storage.setValue(kLastSectionKey, clamped);
    emit lastSectionChanged();
}

void AppSettings::setScriptsDirOverride(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (m_scriptsDirOverride == trimmed) {
        return;
    }
    m_scriptsDirOverride = trimmed;
    m_storage.setValue(kScriptsDirKey, trimmed);
    emit toolPathsChanged();
}

void AppSettings::setPythonOverride(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (m_pythonOverride == trimmed) {
        return;
    }
    m_pythonOverride = trimmed;
    m_storage.setValue(kPythonKey, trimmed);
    emit toolPathsChanged();
}

DatabaseConfig AppSettings::databaseConfig() const
{
    DatabaseConfig config;
    config.backend = m_storage.value(kDbBackendKey).toString() == QStringLiteral("mysql")
                         ? DatabaseConfig::Backend::MySql
                         : DatabaseConfig::Backend::Sqlite;
    config.host = m_storage.value(kDbHostKey).toString();
    config.port = m_storage.value(kDbPortKey, 3306).toInt();
    config.databaseName = m_storage.value(kDbNameKey).toString();
    config.user = m_storage.value(kDbUserKey).toString();
    config.requireTls = m_storage.value(kDbTlsKey, true).toBool();
    return config;
}

void AppSettings::setDatabaseConfig(const DatabaseConfig &config)
{
    m_storage.setValue(kDbBackendKey,
                       config.isMySql() ? QStringLiteral("mysql") : QStringLiteral("sqlite"));
    m_storage.setValue(kDbHostKey, config.host.trimmed());
    m_storage.setValue(kDbPortKey, config.port);
    m_storage.setValue(kDbNameKey, config.databaseName.trimmed());
    m_storage.setValue(kDbUserKey, config.user.trimmed());
    m_storage.setValue(kDbTlsKey, config.requireTls);
    m_storage.sync();
}

void AppSettings::resetToDefaults()
{
    setReducedMotion(false);
    setAmbientEnabled(true);
    setGlassHighlights(true);
    setPerformanceOverlay(false);
    setAmbientSpeed(AmbientSpeed::Normal);
    setBackgroundIntensity(1.0);
}
