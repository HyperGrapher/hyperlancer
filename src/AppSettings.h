#pragma once

#include <QObject>
#include <QSettings>
#include <QVariantMap>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "DatabaseConfig.h"

// User preferences that survive restarts (appearance, tool paths, database connection). Stored in
// an INI file so tests can point it at a temporary location.
class AppSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("AppSettings is created by the application.")

    Q_PROPERTY(
        bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged)
    Q_PROPERTY(bool ambientEnabled READ ambientEnabled WRITE setAmbientEnabled NOTIFY
                   ambientEnabledChanged)
    Q_PROPERTY(bool glassHighlights READ glassHighlights WRITE setGlassHighlights NOTIFY
                   glassHighlightsChanged)
    Q_PROPERTY(QString gmailChromeProfile READ gmailChromeProfile WRITE setGmailChromeProfile NOTIFY
                   gmailChromeProfileChanged)
    // The Convex deployment of the portfolio site (https://<name>.convex.cloud), for the Visitors page.
    Q_PROPERTY(QString convexUrl READ convexUrl WRITE setConvexUrl NOTIFY convexUrlChanged)
    Q_PROPERTY(bool notifyDue READ notifyDue WRITE setNotifyDue NOTIFY notifyDueChanged)
    Q_PROPERTY(bool notifyVisitors READ notifyVisitors WRITE setNotifyVisitors NOTIFY notifyVisitorsChanged)
    Q_PROPERTY(bool notifyNewMail READ notifyNewMail WRITE setNotifyNewMail NOTIFY notifyNewMailChanged)
    Q_PROPERTY(bool soundsEnabled READ soundsEnabled WRITE setSoundsEnabled NOTIFY soundsEnabledChanged)
    Q_PROPERTY(double soundVolume READ soundVolume WRITE setSoundVolume NOTIFY soundVolumeChanged)
    // event id -> sound name; an empty name means no sound for that event.
    Q_PROPERTY(QVariantMap soundAssignments READ soundAssignments NOTIFY soundAssignmentsChanged)
    Q_PROPERTY(bool showFavicons READ showFavicons WRITE setShowFavicons NOTIFY showFaviconsChanged)
    Q_PROPERTY(bool performanceOverlay READ performanceOverlay WRITE setPerformanceOverlay NOTIFY
                   performanceOverlayChanged)
    Q_PROPERTY(AmbientSpeed ambientSpeed READ ambientSpeed WRITE setAmbientSpeed NOTIFY
                   ambientSpeedChanged)
    Q_PROPERTY(double backgroundIntensity READ backgroundIntensity WRITE setBackgroundIntensity
                   NOTIFY backgroundIntensityChanged)
    Q_PROPERTY(int lastSection READ lastSection WRITE setLastSection NOTIFY lastSectionChanged)
    Q_PROPERTY(QString scriptsDirOverride READ scriptsDirOverride WRITE setScriptsDirOverride NOTIFY
                   toolPathsChanged)
    Q_PROPERTY(
        QString pythonOverride READ pythonOverride WRITE setPythonOverride NOTIFY toolPathsChanged)

public:
    enum class AmbientSpeed
    {
        Calm,
        Normal,
        Lively
    };
    Q_ENUM(AmbientSpeed)

    static constexpr int kSectionCount = 9;
    static constexpr double kMinimumIntensity = 0.2;

    explicit AppSettings(const QString &storagePath, QObject *parent = nullptr);

    [[nodiscard]] bool reducedMotion() const { return m_reducedMotion; }
    void setReducedMotion(bool enabled);

    [[nodiscard]] bool ambientEnabled() const { return m_ambientEnabled; }
    void setAmbientEnabled(bool enabled);

    [[nodiscard]] bool glassHighlights() const { return m_glassHighlights; }
    [[nodiscard]] bool notifyNewMail() const { return m_notifyNewMail; }
    [[nodiscard]] QString convexUrl() const { return m_convexUrl; }
    [[nodiscard]] bool notifyVisitors() const { return m_notifyVisitors; }
    [[nodiscard]] bool notifyDue() const { return m_notifyDue; }
    // Keys of the due-date notices already shown (see DueNotifier), so a restart does not repeat them.
    [[nodiscard]] QStringList dueNoticesShown() const;
    void setDueNoticesShown(const QStringList &keys);
    [[nodiscard]] bool soundsEnabled() const { return m_soundsEnabled; }
    [[nodiscard]] double soundVolume() const { return m_soundVolume; }
    [[nodiscard]] QVariantMap soundAssignments() const;
    // The sound for an event: the user's choice, or the event's default.
    [[nodiscard]] Q_INVOKABLE QString soundFor(const QString &eventId) const;
    [[nodiscard]] bool showFavicons() const { return m_showFavicons; }
    [[nodiscard]] QString gmailChromeProfile() const { return m_gmailChromeProfile; }
    void setGlassHighlights(bool enabled);
    void setShowFavicons(bool enabled);
    void setSoundsEnabled(bool enabled);
    void setSoundVolume(double volume);
    Q_INVOKABLE void setSoundFor(const QString &eventId, const QString &soundName);
    void setNotifyNewMail(bool enabled);
    void setConvexUrl(const QString &url);
    void setNotifyVisitors(bool enabled);
    void setNotifyDue(bool enabled);
    void setGmailChromeProfile(const QString &profileName);

    [[nodiscard]] bool performanceOverlay() const { return m_performanceOverlay; }
    void setPerformanceOverlay(bool visible);

    [[nodiscard]] AmbientSpeed ambientSpeed() const { return m_ambientSpeed; }
    void setAmbientSpeed(AmbientSpeed speed);

    [[nodiscard]] double backgroundIntensity() const { return m_backgroundIntensity; }
    void setBackgroundIntensity(double intensity);

    [[nodiscard]] int lastSection() const { return m_lastSection; }
    void setLastSection(int section);

    // Empty means "detect automatically" (see ToolPaths).
    [[nodiscard]] QString scriptsDirOverride() const { return m_scriptsDirOverride; }
    void setScriptsDirOverride(const QString &path);
    [[nodiscard]] QString pythonOverride() const { return m_pythonOverride; }
    void setPythonOverride(const QString &path);

    // Database connection without the password (that lives in the SecretStore).
    // sqlitePath is left empty; the caller fills in the data-folder default.
    [[nodiscard]] DatabaseConfig databaseConfig() const;
    void setDatabaseConfig(const DatabaseConfig &config);

    Q_INVOKABLE void resetToDefaults();

signals:
    void reducedMotionChanged();
    void ambientEnabledChanged();
    void glassHighlightsChanged();
    void showFaviconsChanged();
    void soundsEnabledChanged();
    void soundVolumeChanged();
    void soundAssignmentsChanged();
    void notifyNewMailChanged();
    void convexUrlChanged();
    void notifyVisitorsChanged();
    void notifyDueChanged();
    void gmailChromeProfileChanged();
    void performanceOverlayChanged();
    void ambientSpeedChanged();
    void backgroundIntensityChanged();
    void lastSectionChanged();
    void toolPathsChanged();

private:
    QSettings m_storage;
    bool m_reducedMotion = false;
    bool m_ambientEnabled = true;
    bool m_glassHighlights = true;
    bool m_showFavicons = true;
    bool m_soundsEnabled = true;
    double m_soundVolume = 0.7;
    bool m_notifyNewMail = true;
    QString m_convexUrl;
    bool m_notifyVisitors = true;
    bool m_notifyDue = true;
    QString m_gmailChromeProfile;
    bool m_performanceOverlay = false;
    AmbientSpeed m_ambientSpeed = AmbientSpeed::Normal;
    double m_backgroundIntensity = 1.0;
    int m_lastSection = 0;
    QString m_scriptsDirOverride;
    QString m_pythonOverride;
};
