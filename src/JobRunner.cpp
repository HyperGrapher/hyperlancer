#include "JobRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>

#include "AppSettings.h"
#include "Database.h"
#include "JobStore.h"
#include "ProfileIngest.h"
#include "ToolPaths.h"

namespace {

constexpr int kTickMs = 2000;          // heartbeat and timeout checks
constexpr int kHardKillAfterMs = 5000; // after a soft cancel
constexpr int kProfileLimit = 3;
constexpr int kCrawlLimit = 1;

// Exit codes of the Python tools (scripts/site_tools/exitcodes.py).
constexpr int kExitOk = 0;
constexpr int kExitPartial = 1;
constexpr int kExitInvalidInput = 2;
constexpr int kExitUnreachable = 3;
constexpr int kExitBlocked = 4;
constexpr int kExitCanceled = 130;

QString describeExit(int exitCode)
{
    switch (exitCode) {
    case kExitInvalidInput:
        return QObject::tr("The tool rejected its input (exit 2).");
    case kExitUnreachable:
        return QObject::tr("The site could not be reached (exit 3).");
    case kExitBlocked:
        return QObject::tr("Blocked by the network policy: private or local address (exit 4).");
    default:
        return QObject::tr("The tool stopped unexpectedly (exit %1). See the job log.")
            .arg(exitCode);
    }
}

bool writeSpec(const QString &path, const QJsonObject &spec)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(QJsonDocument(spec).toJson(QJsonDocument::Compact)) > 0;
}

} // namespace

JobRunner::JobRunner(Database &database, const AppPaths &paths, AppSettings &settings,
                     QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_paths(paths)
    , m_settings(settings)
{
    m_tick.setInterval(kTickMs);
    connect(&m_tick, &QTimer::timeout, this, &JobRunner::tick);
}

JobRunner::~JobRunner()
{
    // Children must not outlive the app. Their jobs are marked failed at the next start.
    for (auto &[id, job] : m_running) {
        if (job->process) {
            job->process->disconnect(this);
            job->process->kill();
            job->process->waitForFinished(1000);
        }
    }
}

void JobRunner::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    m_database.run(
        this, [](QSqlDatabase &db) { return JobStore::failInterrupted(db); },
        [this](int interrupted) {
            if (interrupted > 0) {
                emit notice(tr("%n job(s) were interrupted when the app last closed.", nullptr,
                               interrupted),
                            false);
                emit jobsChanged();
            }
            m_tick.start();
            schedulePump();
        });
}

JobRunner::Toolchain JobRunner::toolchain() const
{
    Toolchain tools;
    tools.scriptsDir = m_settings.scriptsDirOverride();
    if (tools.scriptsDir.isEmpty()) {
        tools.scriptsDir = ToolPaths::findScriptsDir(QCoreApplication::applicationDirPath());
    }
    tools.python = m_settings.pythonOverride();
    if (tools.python.isEmpty()) {
        tools.python = ToolPaths::venvPython(tools.scriptsDir);
    }
    return tools;
}

void JobRunner::enqueueProfile(qint64 siteId, const QString &url,
                               std::function<void(qint64, QString)> done)
{
    const QJsonObject params{{QStringLiteral("url"), url}, {QStringLiteral("timeout"), 12}};
    m_database.run(
        this,
        [siteId, params](QSqlDatabase &db) {
            return JobStore::create(db, QStringLiteral("profile"), siteId, params);
        },
        [this, done = std::move(done)](const JobStore::CreateResult &result) {
            if (done) {
                done(result.jobId, result.error);
            }
            if (result.error.isEmpty() && !result.alreadyQueued) {
                emit jobsChanged();
                schedulePump();
            }
        });
}

void JobRunner::cancel(qint64 jobId)
{
    const auto found = m_running.find(jobId);
    if (found != m_running.end()) {
        found->second->canceled = true;
        softCancel(*found->second);
        return;
    }
    m_database.run(
        this, [jobId](QSqlDatabase &db) { return JobStore::cancelQueued(db, jobId); },
        [this](bool canceled) {
            if (canceled) {
                emit jobsChanged();
            }
        });
}

void JobRunner::retry(qint64 jobId)
{
    m_database.run(
        this, [jobId](QSqlDatabase &db) { return JobStore::retry(db, jobId); },
        [this](qint64 newId) {
            if (newId > 0) {
                emit jobsChanged();
                schedulePump();
            }
        });
}

void JobRunner::cancelAll()
{
    for (auto &[id, job] : m_running) {
        job->canceled = true;
        softCancel(*job);
    }
}

void JobRunner::schedulePump()
{
    if (!m_started || m_pumpScheduled) {
        return;
    }
    m_pumpScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_pumpScheduled = false;
        pump();
    });
}

void JobRunner::pump()
{
    if (m_pumpInFlight) {
        // A pump is reading the queue; run once more afterwards to see newer jobs.
        m_pumpScheduled = true;
        return;
    }
    m_pumpInFlight = true;
    m_database.run(
        this, [](QSqlDatabase &db) { return JobStore::queued(db, 50); },
        [this](const std::vector<JobStore::QueuedJob> &jobs) {
            m_pumpInFlight = false;
            for (const JobStore::QueuedJob &job : jobs) {
                if (m_running.contains(job.id) || runningOfType(job.type) >= limitFor(job.type)) {
                    continue;
                }
                launch(job.id, job.type, job.siteId, job.params);
            }
            if (m_pumpScheduled) {
                m_pumpScheduled = false;
                pump();
            }
        });
}

void JobRunner::launch(qint64 jobId, const QString &type, qint64 siteId, const QJsonObject &params)
{
    if (type != QStringLiteral("profile")) {
        failBeforeStart(jobId, siteId, type, tr("Job type '%1' is not supported yet.").arg(type));
        return;
    }
    const Toolchain tools = toolchain();
    if (tools.scriptsDir.isEmpty() || !QFileInfo::exists(tools.scriptsDir)) {
        failBeforeStart(jobId, siteId, type,
                        tr("Python tools folder not found. Set it in Settings → Tools."));
        return;
    }
    if (!QFileInfo(tools.python).isExecutable()) {
        failBeforeStart(jobId, siteId, type,
                        tr("Python not found at %1. Run \"uv sync\" in the tools folder or set the "
                           "path in Settings → Tools.")
                            .arg(QDir::toNativeSeparators(tools.python)));
        return;
    }

    auto job = std::make_unique<RunningJob>();
    job->id = jobId;
    job->type = type;
    job->siteId = siteId;
    job->dir = m_paths.jobDir(jobId);
    QDir().mkpath(job->dir);
    job->cancelFile = QDir(job->dir).filePath(QStringLiteral("cancel"));
    job->resultPath = QDir(job->dir).filePath(QStringLiteral("profile.json"));
    QFile::remove(job->cancelFile);
    QFile::remove(job->resultPath);

    const QString specPath = QDir(job->dir).filePath(QStringLiteral("spec.json"));
    const QJsonObject spec{{QStringLiteral("jobId"), jobId},
                           {QStringLiteral("type"), type},
                           {QStringLiteral("params"), params},
                           {QStringLiteral("cancelFile"), job->cancelFile},
                           {QStringLiteral("dir"), job->dir}};
    if (!writeSpec(specPath, spec)) {
        failBeforeStart(jobId, siteId, type,
                        tr("Could not write the job spec in %1.").arg(job->dir));
        return;
    }

    job->process = std::make_unique<QProcess>();
    QProcess &process = *job->process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    process.setProcessEnvironment(environment);
    process.setWorkingDirectory(tools.scriptsDir);
    process.setProgram(tools.python);
    process.setArguments(
        {QStringLiteral("-m"), QStringLiteral("site_profile"), QStringLiteral("--spec"), specPath});
    const QString logPath = QDir(job->dir).filePath(QStringLiteral("job.log"));
    process.setStandardErrorFile(logPath, QIODevice::Append);
    process.setStandardInputFile(QProcess::nullDevice());

    connect(&process, &QProcess::readyReadStandardOutput, this, [this, jobId]() {
        const auto found = m_running.find(jobId);
        if (found != m_running.end()) {
            handleOutput(*found->second);
        }
    });
    connect(&process, &QProcess::finished, this,
            [this, jobId](int exitCode, QProcess::ExitStatus status) {
                handleExit(jobId, exitCode, status);
            });
    connect(&process, &QProcess::errorOccurred, this, [this, jobId](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            handleExit(jobId, -1, QProcess::CrashExit);
        }
    });

    const QString relativeLog = QDir(m_paths.dataDir).relativeFilePath(logPath);
    m_database.post(
        [jobId, relativeLog](QSqlDatabase &db) { JobStore::markStarted(db, jobId, relativeLog); });

    job->clock.start();
    RunningJob &stored = *job;
    m_running.emplace(jobId, std::move(job));
    emit runningChanged();
    emit jobsChanged();
    emit jobProgress(jobId, siteId, 0, 0, tr("starting"));
    qInfo("job %lld: started %s (site %lld)", jobId, qPrintable(type), siteId);
    stored.process->start();
}

void JobRunner::handleOutput(RunningJob &job)
{
    const JsonLines::Batch batch = job.lines.feed(job.process->readAllStandardOutput());
    for (const QByteArray &line : batch.rejected) {
        qWarning("job %lld: ignored non-JSON output: %s", job.id, line.constData());
    }
    for (const QJsonObject &event : batch.events) {
        handleEvent(job, event);
    }
}

void JobRunner::handleEvent(RunningJob &job, const QJsonObject &event)
{
    const QString type = event.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("progress")) {
        const int current = event.value(QStringLiteral("current")).toInt();
        const int total = event.value(QStringLiteral("total")).toInt();
        const QString label = event.value(QStringLiteral("label")).toString();
        const qint64 jobId = job.id;
        m_database.post([jobId, current, total, label](QSqlDatabase &db) {
            JobStore::updateProgress(db, jobId, current, total, label);
        });
        emit jobProgress(job.id, job.siteId, current, total, label);
    } else if (type == QStringLiteral("result")) {
        const QString path = event.value(QStringLiteral("path")).toString();
        if (!path.isEmpty()) {
            job.resultPath = path;
        }
    } else if (type == QStringLiteral("log")) {
        qInfo("job %lld [%s]: %s", job.id,
              qPrintable(event.value(QStringLiteral("level")).toString()),
              qPrintable(event.value(QStringLiteral("message")).toString()));
    }
}

void JobRunner::handleExit(qint64 jobId, int exitCode, QProcess::ExitStatus exitStatus)
{
    const auto found = m_running.find(jobId);
    if (found == m_running.end()) {
        return;
    }
    std::unique_ptr<RunningJob> job = std::move(found->second);
    m_running.erase(found);
    // Read what is left before the process object goes away.
    if (job->process->state() == QProcess::NotRunning) {
        handleOutput(*job);
        for (const QJsonObject &event : job->lines.finish().events) {
            handleEvent(*job, event);
        }
    }
    const QString processError =
        job->process->error() == QProcess::FailedToStart ? job->process->errorString() : QString();
    job->process->disconnect(this);
    // Deleting a QProcess from its own finished() signal is unsafe; let the event loop do it.
    job->process.release()->deleteLater();

    QString status;
    QString error;
    if (!processError.isEmpty()) {
        status = QStringLiteral("failed");
        error = tr("Could not start Python: %1").arg(processError);
    } else if (job->timedOut) {
        status = QStringLiteral("failed");
        error = tr("Timed out after %1 seconds.").arg(timeoutSeconds(job->type));
    } else if (job->canceled || exitCode == kExitCanceled) {
        status = QStringLiteral("canceled");
    } else if (exitStatus == QProcess::CrashExit) {
        status = QStringLiteral("failed");
        error = tr("The tool crashed. See the job log.");
    } else if (exitCode == kExitOk) {
        status = QStringLiteral("succeeded");
    } else if (exitCode == kExitPartial) {
        status = QStringLiteral("partial");
    } else {
        status = QStringLiteral("failed");
        error = describeExit(exitCode);
    }
    qInfo("job %lld: finished %s exit=%d", jobId, qPrintable(status), exitCode);

    const qint64 siteId = job->siteId;
    const QString resultPath = job->resultPath;
    // A failed profile usually still wrote a report (unreachable, blocked); store it so the
    // history shows what happened. Canceled jobs leave no snapshot.
    const bool ingest =
        job->type == QStringLiteral("profile") && status != QStringLiteral("canceled");
    m_database.run(
        this,
        [jobId, siteId, status, exitCode, error, resultPath, ingest](QSqlDatabase &db) {
            QJsonObject result;
            if (ingest) {
                const ProfileIngest::Result stored = ProfileIngest::ingestFile(
                    db, siteId, jobId, resultPath,
                    error.isEmpty() ? QObject::tr("No report was written.") : error);
                result.insert(QStringLiteral("snapshotId"), stored.snapshotId);
                result.insert(QStringLiteral("outcome"), stored.outcome);
                if (!stored.error.isEmpty()) {
                    result.insert(QStringLiteral("ingestError"), stored.error);
                }
            }
            JobStore::finish(db, jobId, status, exitCode, error, result);
            return result.value(QStringLiteral("ingestError")).toString();
        },
        [this, jobId, siteId, status](const QString &ingestError) {
            if (!ingestError.isEmpty()) {
                emit notice(tr("Could not store the analysis: %1").arg(ingestError), true);
            }
            emit jobFinished(jobId, siteId, status);
            emit jobsChanged();
        });

    emit runningChanged();
    schedulePump();
}

void JobRunner::failBeforeStart(qint64 jobId, qint64 siteId, const QString &type,
                                const QString &message)
{
    qWarning("job %lld: %s", jobId, qPrintable(message));
    const bool isProfile = type == QStringLiteral("profile");
    m_database.run(
        this,
        [jobId, siteId, message, isProfile](QSqlDatabase &db) {
            if (isProfile && siteId > 0) {
                const ProfileIngest::Result stored =
                    ProfileIngest::ingestFailure(db, siteId, jobId, message);
                Q_UNUSED(stored)
            }
            JobStore::finish(db, jobId, QStringLiteral("failed"), -1, message, {});
            return true;
        },
        [this, jobId, siteId, message](bool) {
            emit notice(message, true);
            emit jobFinished(jobId, siteId, QStringLiteral("failed"));
            emit jobsChanged();
        });
}

void JobRunner::softCancel(RunningJob &job)
{
    if (job.cancelAtMs >= 0) {
        return;
    }
    QFile marker(job.cancelFile);
    if (marker.open(QIODevice::WriteOnly)) {
        marker.close();
    }
    job.cancelAtMs = job.clock.elapsed();
    qInfo("job %lld: %s, stopping", job.id, job.timedOut ? "timeout" : "cancel requested");
}

void JobRunner::tick()
{
    QList<qint64> alive;
    for (auto &[id, job] : m_running) {
        alive.append(id);
        if (job->cancelAtMs < 0 && job->clock.elapsed() > timeoutSeconds(job->type) * 1000LL) {
            job->timedOut = true;
            softCancel(*job);
        }
        if (job->cancelAtMs >= 0 && job->clock.elapsed() - job->cancelAtMs > kHardKillAfterMs &&
            job->process && job->process->state() != QProcess::NotRunning) {
            qWarning("job %lld: did not stop after cancel, killing", id);
            job->process->kill();
        }
    }
    if (!alive.isEmpty()) {
        m_database.post([alive](QSqlDatabase &db) { JobStore::heartbeat(db, alive); });
    }
}

int JobRunner::limitFor(const QString &type) const
{
    return type == QStringLiteral("crawl") ? kCrawlLimit : kProfileLimit;
}

int JobRunner::runningOfType(const QString &type) const
{
    int count = 0;
    for (const auto &[id, job] : m_running) {
        if (job->type == type) {
            ++count;
        }
    }
    return count;
}

int JobRunner::timeoutSeconds(const QString &type)
{
    return type == QStringLiteral("crawl") ? 2 * 60 * 60 : 120;
}
