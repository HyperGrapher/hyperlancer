#pragma once

#include <map>
#include <memory>

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include "AppPaths.h"
#include "JsonLines.h"

class AppSettings;
class Database;

// Runs the Python tools as child processes (replaces the PHP worker). The jobs table is the
// queue; this class starts queued jobs within per-type limits, follows their NDJSON events,
// enforces timeouts, handles cancel, and ingests the result. Everything here is event
// driven on the UI thread; database work goes through Database::run.
class JobRunner : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("JobRunner is created by the application.")

    Q_PROPERTY(int runningCount READ runningCount NOTIFY runningChanged)

public:
    struct Toolchain
    {
        QString python;
        QString scriptsDir;
    };

    JobRunner(Database &database, const AppPaths &paths, AppSettings &settings,
              QObject *parent = nullptr);
    ~JobRunner() override;

    // Call once the database is ready: fails jobs left running by an earlier session and
    // starts the queue.
    void start();

    [[nodiscard]] int runningCount() const { return static_cast<int>(m_running.size()); }
    [[nodiscard]] Toolchain toolchain() const;

    // Queues a profile job for the site. done(jobId, error) runs on the UI thread.
    void enqueueProfile(qint64 siteId, const QString &url,
                        std::function<void(qint64, QString)> done = {});

    // Queues a content crawl (site_crawl): Markdown pages, images and documents of the site,
    // up to maxPages pages. done(jobId, error) runs on the UI thread.
    void enqueueCrawl(qint64 siteId, const QString &url, int maxPages,
                      std::function<void(qint64, QString)> done = {});

    // Queues a browser render (site_render): desktop, mobile and full-page screenshots plus the
    // trackers the page loads at run time. done(jobId, error) runs on the UI thread.
    void enqueueRender(qint64 siteId, const QString &url,
                       std::function<void(qint64, QString)> done = {});

    Q_INVOKABLE void cancel(qint64 jobId);
    Q_INVOKABLE void retry(qint64 jobId);
    // Asks every running job to stop (used when the app quits).
    Q_INVOKABLE void cancelAll();

signals:
    void runningChanged();
    // Any change to a job row that a list should pick up (queued, started, finished).
    void jobsChanged();
    void jobProgress(qint64 jobId, qint64 siteId, int current, int total, const QString &label);
    void jobFinished(qint64 jobId, qint64 siteId, const QString &status);
    // The same two events for content crawls, kept apart so a crawl never looks like an
    // analysis in the places that listen for those.
    void crawlProgress(qint64 jobId, qint64 siteId, int current, int total, const QString &label);
    void crawlFinished(qint64 jobId, qint64 siteId, const QString &status);
    // And for browser renders.
    void renderProgress(qint64 jobId, qint64 siteId, int current, int total, const QString &label);
    void renderFinished(qint64 jobId, qint64 siteId, const QString &status);
    void notice(const QString &message, bool isError);

private:
    struct RunningJob
    {
        qint64 id = 0;
        QString type;
        qint64 siteId = 0;
        QString dir;
        QString cancelFile;
        QString resultPath;
        std::unique_ptr<QProcess> process;
        JsonLines lines;
        QElapsedTimer clock;
        qint64 cancelAtMs = -1; // clock time of the soft cancel, -1 if none
        bool timedOut = false;
        bool canceled = false;
    };

    void schedulePump();
    void pump();
    void launch(qint64 jobId, const QString &type, qint64 siteId, const QJsonObject &params);
    void handleOutput(RunningJob &job);
    void handleEvent(RunningJob &job, const QJsonObject &event);
    void handleExit(qint64 jobId, int exitCode, QProcess::ExitStatus exitStatus);
    void failBeforeStart(qint64 jobId, qint64 siteId, const QString &type, const QString &message);
    void reportProgress(const QString &type, qint64 jobId, qint64 siteId, int current, int total,
                        const QString &label);
    void reportFinished(const QString &type, qint64 jobId, qint64 siteId, const QString &status);
    void softCancel(RunningJob &job);
    void tick();
    [[nodiscard]] int limitFor(const QString &type) const;
    [[nodiscard]] int runningOfType(const QString &type) const;
    [[nodiscard]] static int timeoutSeconds(const QString &type);

    Database &m_database;
    AppPaths m_paths;
    AppSettings &m_settings;
    std::map<qint64, std::unique_ptr<RunningJob>> m_running;
    QTimer m_tick;
    bool m_started = false;
    bool m_pumpScheduled = false;
    bool m_pumpInFlight = false;
};
