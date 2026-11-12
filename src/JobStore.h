#pragma once

#include <vector>

#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>

// The jobs table is the queue. These functions run on the database thread.
namespace JobStore {

struct QueuedJob
{
    qint64 id = 0;
    QString type;
    qint64 siteId = 0;
    QJsonObject params;
};

struct JobRow
{
    qint64 id = 0;
    QString type;
    qint64 siteId = 0;
    QString siteKey;
    QString status;
    int progressCurrent = 0;
    int progressTotal = 0;
    QString progressLabel;
    QString error;
    QString createdAt;
    QString startedAt;
    QString finishedAt;
    int exitCode = 0;
    bool hasExitCode = false;
};

struct CreateResult
{
    qint64 jobId = 0;
    bool alreadyQueued = false;
    QString error;
};

// Queues a job. A profile job for a site that already has one queued or running returns that
// job instead, so double clicks do not stack up work.
[[nodiscard]] CreateResult create(QSqlDatabase &db, const QString &type, qint64 siteId,
                                  const QJsonObject &params);
// Queued jobs, oldest first.
[[nodiscard]] std::vector<QueuedJob> queued(QSqlDatabase &db, int limit);
void markStarted(QSqlDatabase &db, qint64 jobId, const QString &logPath);
void updateProgress(QSqlDatabase &db, qint64 jobId, int current, int total, const QString &label);
void heartbeat(QSqlDatabase &db, const QList<qint64> &jobIds);
void finish(QSqlDatabase &db, qint64 jobId, const QString &status, int exitCode,
            const QString &error, const QJsonObject &result);
// A queued job is canceled right away; returns false when it was not queued.
bool cancelQueued(QSqlDatabase &db, qint64 jobId);
// Jobs still marked running belong to an earlier session that ended without finishing them.
int failInterrupted(QSqlDatabase &db);
// Queues a copy of a finished job. Returns the new id, or 0.
[[nodiscard]] qint64 retry(QSqlDatabase &db, qint64 jobId);
[[nodiscard]] std::vector<JobRow> recent(QSqlDatabase &db, int limit);

[[nodiscard]] bool isFinalStatus(const QString &status);

} // namespace JobStore
