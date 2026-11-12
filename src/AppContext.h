#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "AppPaths.h"
#include "Dashboard.h"
#include "Database.h"
#include "Discovery.h"
#include "GmailInbox.h"
#include "SoundPlayer.h"
#include "GmailService.h"
#include "HealthModel.h"
#include "Integrations.h"
#include "JobListModel.h"
#include "JobRunner.h"
#include "LangSearchClient.h"
#include "PageSpeedClient.h"
#include "SecretStore.h"
#include "SiteDetail.h"
#include "SiteListModel.h"

class AppSettings;

// Makes the core library's Database visible to QML (its enum and properties) without the
// core library depending on Qt Qml.
struct DatabaseForeign
{
    Q_GADGET
    QML_FOREIGN(Database)
    QML_NAMED_ELEMENT(Database)
    QML_UNCREATABLE("Database is created by the application.")
};

// Owns every service of the app and hands them to QML. Construction order is the member
// order below: each member only refers to members declared before it.
class AppContext : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("AppContext is created by the application.")

    Q_PROPERTY(Database *database READ database CONSTANT)
    Q_PROPERTY(JobRunner *jobRunner READ jobRunner CONSTANT)
    Q_PROPERTY(SiteListModel *sites READ sites CONSTANT)
    Q_PROPERTY(JobListModel *jobs READ jobs CONSTANT)
    Q_PROPERTY(SiteDetail *siteDetail READ siteDetail CONSTANT)
    Q_PROPERTY(Dashboard *dashboard READ dashboard CONSTANT)
    Q_PROPERTY(Discovery *discovery READ discovery CONSTANT)
    Q_PROPERTY(Integrations *integrations READ integrations CONSTANT)
    Q_PROPERTY(GmailService *gmail READ gmail CONSTANT)
    Q_PROPERTY(SoundPlayer *sounds READ sounds CONSTANT)
    Q_PROPERTY(GmailInbox *inbox READ inbox CONSTANT)
    Q_PROPERTY(HealthModel *health READ health CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    AppContext(const AppPaths &paths, AppSettings &settings, QObject *parent = nullptr);
    ~AppContext() override;

    // Connects to the configured database; everything else starts once it is ready.
    void start();

    [[nodiscard]] Database *database() { return &m_database; }
    [[nodiscard]] JobRunner *jobRunner() { return &m_runner; }
    [[nodiscard]] SiteListModel *sites() { return &m_sites; }
    [[nodiscard]] JobListModel *jobs() { return &m_jobs; }
    [[nodiscard]] SiteDetail *siteDetail() { return &m_siteDetail; }
    [[nodiscard]] Dashboard *dashboard() { return &m_dashboard; }
    [[nodiscard]] Discovery *discovery() { return &m_discovery; }
    [[nodiscard]] Integrations *integrations() { return &m_integrations; }
    [[nodiscard]] GmailService *gmail() { return &m_gmail; }
    [[nodiscard]] SoundPlayer *sounds() { return &m_sounds; }
    [[nodiscard]] GmailInbox *inbox() { return &m_inbox; }
    [[nodiscard]] HealthModel *health() { return &m_health; }
    [[nodiscard]] QString version() const;

    // Adds the site typed by the user and, when analyze is set, queues its analysis.
    // Emits siteAdded on success and notice on failure.
    Q_INVOKABLE void addSite(const QString &text, bool analyze);
    // Adds every address in the text (separated by spaces, commas, semicolons or new lines).
    // Emits sitesAdded with the counts and the entries that are not valid addresses.
    Q_INVOKABLE void addSites(const QString &text, bool analyze);
    Q_INVOKABLE void removeSites(const QVariantList &siteIds);
    Q_INVOKABLE void analyzeSite(qint64 siteId, const QString &url);
    Q_INVOKABLE void removeSite(qint64 siteId);
    Q_INVOKABLE void openExternal(const QString &url) const;
    Q_INVOKABLE void copyText(const QString &text) const;
    Q_INVOKABLE void openDataFolder() const;
    // Asks the main window to show the Emails section (used by the mail notification).
    Q_INVOKABLE void requestOpenEmails() { emit openEmailsRequested(); }
    Q_INVOKABLE void openJobLog(qint64 jobId) const;
    // Starts a new instance with the same arguments and quits this one.
    Q_INVOKABLE void restart();

signals:
    void notice(const QString &message, bool isError);
    void siteAdded(qint64 siteId, bool created);
    void sitesAdded(int added, int existing, const QStringList &invalid);
    void siteRemoved(qint64 siteId);
    void openEmailsRequested();

private:
    void onDatabaseReady();
    [[nodiscard]] DatabaseConfig databaseConfig() const;

    AppPaths m_paths;
    AppSettings &m_settings;
    SoundPlayer m_sounds;
    QNetworkAccessManager m_network;
    SecretStore m_secrets;
    Database m_database;
    JobRunner m_runner;
    LangSearchClient m_langSearch;
    PageSpeedClient m_pageSpeed;
    Integrations m_integrations;
    GmailService m_gmail;
    GmailInbox m_inbox;
    SiteListModel m_sites;
    JobListModel m_jobs;
    SiteDetail m_siteDetail;
    Dashboard m_dashboard;
    Discovery m_discovery;
    HealthModel m_health;
};
