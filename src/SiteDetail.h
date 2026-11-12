#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class Integrations;
class Database;
class GmailService;
class JobRunner;
class PageSpeedClient;

// The site shown on the detail view: its row, latest snapshot summary, history, activity,
// live analysis progress, speed check and matched emails. Loads on the database thread.
class SiteDetail : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("SiteDetail is created by the application.")

    Q_PROPERTY(qint64 siteId READ siteId WRITE setSiteId NOTIFY siteIdChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadedChanged)
    Q_PROPERTY(QString error READ error NOTIFY loadedChanged)
    Q_PROPERTY(QVariantMap site READ site NOTIFY loadedChanged)
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY loadedChanged)
    Q_PROPERTY(QVariantList snapshots READ snapshots NOTIFY loadedChanged)
    Q_PROPERTY(QVariantList activity READ activity NOTIFY loadedChanged)
    Q_PROPERTY(QVariantMap speed READ speed NOTIFY loadedChanged)
    Q_PROPERTY(bool analyzing READ isAnalyzing NOTIFY progressChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressLabel READ progressLabel NOTIFY progressChanged)
    Q_PROPERTY(int progressStep READ progressStep NOTIFY progressChanged)
    Q_PROPERTY(bool speedRunning READ isSpeedRunning NOTIFY speedRunningChanged)
    Q_PROPERTY(QVariantList emails READ emails NOTIFY emailsChanged)
    Q_PROPERTY(bool emailsLoading READ emailsLoading NOTIFY emailsChanged)
    Q_PROPERTY(QString emailsError READ emailsError NOTIFY emailsChanged)

public:
    SiteDetail(Database &database, JobRunner &runner, PageSpeedClient &pageSpeed,
               Integrations &integrations, GmailService &gmail, QObject *parent = nullptr);

    [[nodiscard]] qint64 siteId() const { return m_siteId; }
    void setSiteId(qint64 siteId);
    [[nodiscard]] bool isLoading() const { return m_loading; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] QVariantMap site() const { return m_site; }
    [[nodiscard]] QVariantMap snapshot() const { return m_snapshot; }
    [[nodiscard]] QVariantList snapshots() const { return m_snapshots; }
    [[nodiscard]] QVariantList activity() const { return m_activity; }
    [[nodiscard]] QVariantMap speed() const { return m_speed; }
    [[nodiscard]] bool isAnalyzing() const { return m_analyzing; }
    [[nodiscard]] double progress() const { return m_progress; }
    [[nodiscard]] QString progressLabel() const { return m_progressLabel; }
    [[nodiscard]] int progressStep() const { return m_progressStep; }
    [[nodiscard]] bool isSpeedRunning() const { return m_speedRunning; }
    [[nodiscard]] QVariantList emails() const { return m_emails; }
    [[nodiscard]] bool emailsLoading() const { return m_emailsLoading; }
    [[nodiscard]] QString emailsError() const { return m_emailsError; }

    Q_INVOKABLE void reload();
    Q_INVOKABLE void analyze();
    Q_INVOKABLE void checkSpeed();
    Q_INVOKABLE void loadEmails();
    // Shows an older snapshot instead of the latest.
    Q_INVOKABLE void showSnapshot(qint64 snapshotId);

signals:
    void siteIdChanged();
    void loadedChanged();
    void progressChanged();
    void speedRunningChanged();
    void emailsChanged();
    void notice(const QString &message, bool isError);

private:
    void setProgress(bool analyzing, double progress, const QString &label, int step);

    Database &m_database;
    JobRunner &m_runner;
    PageSpeedClient &m_pageSpeed;
    Integrations &m_integrations;
    GmailService &m_gmail;
    qint64 m_siteId = 0;
    int m_generation = 0;
    bool m_loading = false;
    QString m_error;
    QVariantMap m_site;
    QVariantMap m_snapshot;
    QVariantList m_snapshots;
    QVariantList m_activity;
    QVariantMap m_speed;
    bool m_analyzing = false;
    double m_progress = 0.0;
    QString m_progressLabel;
    int m_progressStep = 0;
    bool m_speedRunning = false;
    QVariantList m_emails;
    bool m_emailsLoading = false;
    QString m_emailsError;
};
