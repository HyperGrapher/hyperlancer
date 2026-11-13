#pragma once

#include <functional>

#include <QObject>
#include <QSqlDatabase>
#include <QStringList>
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
    // Result of compareSnapshot: SnapshotDiff groups, or { error }. Empty while closed.
    Q_PROPERTY(QVariantMap snapshotDiff READ snapshotDiff NOTIFY snapshotDiffChanged)
    Q_PROPERTY(bool diffLoading READ isDiffLoading NOTIFY snapshotDiffChanged)
    Q_PROPERTY(QVariantMap speed READ speed NOTIFY loadedChanged)
    Q_PROPERTY(QVariantMap speedDesktop READ speedDesktop NOTIFY loadedChanged)
    // "mobile" and "desktop": { url (file URL), createdAt } of the newest screenshot.
    Q_PROPERTY(QVariantMap screenshots READ screenshots NOTIFY loadedChanged)
    Q_PROPERTY(QVariantMap organizer READ organizer NOTIFY loadedChanged)
    Q_PROPERTY(QStringList statuses READ statuses CONSTANT)
    // The latest content crawl (see CrawlStore::latest) and its pages; empty when never crawled.
    Q_PROPERTY(QVariantMap crawl READ crawl NOTIFY loadedChanged)
    Q_PROPERTY(QVariantList crawlPages READ crawlPages NOTIFY loadedChanged)
    // The latest browser render (title, loadMs, requestCount, trackers); empty when never rendered.
    Q_PROPERTY(QVariantMap render READ render NOTIFY loadedChanged)
    Q_PROPERTY(bool renderRunning READ isRenderRunning NOTIFY renderRunningChanged)
    Q_PROPERTY(bool crawlRunning READ isCrawlRunning NOTIFY crawlProgressChanged)
    Q_PROPERTY(double crawlFraction READ crawlFraction NOTIFY crawlProgressChanged)
    Q_PROPERTY(QString crawlLabel READ crawlLabel NOTIFY crawlProgressChanged)
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
               Integrations &integrations, GmailService &gmail, QString dataDir,
               QObject *parent = nullptr);

    [[nodiscard]] qint64 siteId() const { return m_siteId; }
    void setSiteId(qint64 siteId);
    [[nodiscard]] bool isLoading() const { return m_loading; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] QVariantMap site() const { return m_site; }
    [[nodiscard]] QVariantMap snapshot() const { return m_snapshot; }
    [[nodiscard]] QVariantList snapshots() const { return m_snapshots; }
    [[nodiscard]] QVariantList activity() const { return m_activity; }
    [[nodiscard]] QVariantMap snapshotDiff() const { return m_snapshotDiff; }
    [[nodiscard]] bool isDiffLoading() const { return m_diffLoading; }
    [[nodiscard]] QVariantMap speed() const { return m_speed; }
    [[nodiscard]] QVariantMap speedDesktop() const { return m_speedDesktop; }
    [[nodiscard]] QVariantMap screenshots() const { return m_screenshots; }
    [[nodiscard]] QVariantMap organizer() const { return m_organizer; }
    [[nodiscard]] QStringList statuses() const;
    [[nodiscard]] QVariantMap crawl() const { return m_crawl; }
    [[nodiscard]] QVariantList crawlPages() const { return m_crawlPages; }
    [[nodiscard]] QVariantMap render() const { return m_render; }
    [[nodiscard]] bool isRenderRunning() const { return m_renderRunning; }
    [[nodiscard]] bool isCrawlRunning() const { return m_crawlRunning; }
    [[nodiscard]] double crawlFraction() const { return m_crawlFraction; }
    [[nodiscard]] QString crawlLabel() const { return m_crawlLabel; }
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
    // Runs PageSpeed Insights for mobile and desktop: two scores and two screenshots.
    Q_INVOKABLE void checkSpeed();
    Q_INVOKABLE void loadEmails();
    // Crawls the website's pages into Markdown (up to maxPages) and downloads its images and
    // documents; the result is the "Content" tab.
    Q_INVOKABLE void startCrawl(int maxPages);
    // Opens the page in a headless browser: desktop, mobile and full-page screenshots, plus the
    // trackers the page loads at run time.
    Q_INVOKABLE void startRender();
    Q_INVOKABLE void cancelCrawl();

    // Status, category, tags, notes and to-dos. Each saves, then reloads the detail.
    Q_INVOKABLE void setStatus(const QString &status);
    Q_INVOKABLE void setCategory(const QString &name);
    Q_INVOKABLE void addTag(const QString &name);
    Q_INVOKABLE void removeTag(qint64 tagId);
    Q_INVOKABLE void setDeadline(const QString &dueOn);
    Q_INVOKABLE void addNote(const QString &title, const QString &body);
    Q_INVOKABLE void updateNote(qint64 noteId, const QString &title, const QString &body);
    Q_INVOKABLE void deleteNote(qint64 noteId);
    Q_INVOKABLE void addTodo(const QString &title, const QString &dueOn);
    Q_INVOKABLE void setTodoDone(qint64 todoId, bool done);
    Q_INVOKABLE void deleteTodo(qint64 todoId);
    // What changed in this analysis compared with the one before it.
    Q_INVOKABLE void compareSnapshot(qint64 snapshotId);
    Q_INVOKABLE void closeComparison();
    // Shows an older snapshot instead of the latest.
    Q_INVOKABLE void showSnapshot(qint64 snapshotId);

signals:
    void siteIdChanged();
    void loadedChanged();
    void progressChanged();
    void crawlProgressChanged();
    void renderRunningChanged();
    void speedRunningChanged();
    void snapshotDiffChanged();
    // Both PageSpeed calls answered; ok is false when either failed.
    void speedCheckFinished(bool ok);
    void emailsChanged();
    void notice(const QString &message, bool isError);
    // A status, category, tag or to-do changed: the Websites list should refresh.
    void organizerChanged();

private:
    void saveOrganizer(std::function<bool(QSqlDatabase &)> change);
    void setCrawlProgress(bool running, double fraction, const QString &label);
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
    QVariantMap m_crawl;
    QVariantList m_crawlPages;
    QVariantMap m_render;
    bool m_renderRunning = false;
    bool m_crawlRunning = false;
    double m_crawlFraction = 0.0;
    QString m_crawlLabel;
    qint64 m_crawlJobId = 0;
    QVariantMap m_snapshotDiff;
    bool m_diffLoading = false;
    QVariantMap m_speedDesktop;
    QVariantMap m_screenshots;
    QString m_dataDir;
    QVariantMap m_organizer;
    bool m_analyzing = false;
    double m_progress = 0.0;
    QString m_progressLabel;
    int m_progressStep = 0;
    bool m_speedRunning = false;
    QVariantList m_emails;
    bool m_emailsLoading = false;
    QString m_emailsError;
};
