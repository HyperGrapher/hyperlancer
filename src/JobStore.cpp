#include "JobStore.h"

#include <QJsonDocument>

#include "DbTime.h"
#include "SqlExec.h"

namespace JobStore {

namespace {

QString toJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonObject fromJson(const QVariant &value)
{
    const QJsonDocument document = QJsonDocument::fromJson(value.toString().toUtf8());
    return document.isObject() ? document.object() : QJsonObject();
}

} // namespace

CreateResult create(QSqlDatabase &db, const QString &type, qint64 siteId, const QJsonObject &params)
{
    CreateResult result;
    if (siteId > 0) {
        QSqlQuery pending = SqlExec::run(
            db,
            QStringLiteral("SELECT id FROM jobs WHERE site_id = ? AND type = ? AND status IN "
                           "('queued','running') ORDER BY id DESC LIMIT 1"),
            {siteId, type}, &result.error);
        if (pending.next()) {
            result.jobId = pending.value(0).toLongLong();
            result.alreadyQueued = true;
            return result;
        }
    }
    QSqlQuery insert = SqlExec::run(
        db,
        QStringLiteral("INSERT INTO jobs (type, site_id, status, params_json, created_at) VALUES "
                       "(?, ?, 'queued', ?, ?)"),
        {type, siteId > 0 ? QVariant(siteId) : SqlExec::nullInt(), toJson(params), DbTime::now()},
        &result.error);
    if (result.error.isEmpty()) {
        result.jobId = insert.lastInsertId().toLongLong();
    }
    return result;
}

std::vector<QueuedJob> queued(QSqlDatabase &db, int limit)
{
    std::vector<QueuedJob> jobs;
    QSqlQuery query = SqlExec::run(
        db,
        QStringLiteral("SELECT id, type, site_id, params_json FROM jobs WHERE status = 'queued' "
                       "ORDER BY id LIMIT %1")
            .arg(limit));
    while (query.next()) {
        jobs.push_back({query.value(0).toLongLong(), query.value(1).toString(),
                        query.value(2).toLongLong(), fromJson(query.value(3))});
    }
    return jobs;
}

void markStarted(QSqlDatabase &db, qint64 jobId, const QString &logPath)
{
    const QString now = DbTime::now();
    SqlExec::run(
        db,
        QStringLiteral("UPDATE jobs SET status = 'running', started_at = ?, heartbeat_at = ?, "
                       "attempts = attempts + 1, log_path = ? WHERE id = ?"),
        {now, now, logPath, jobId});
}

void updateProgress(QSqlDatabase &db, qint64 jobId, int current, int total, const QString &label)
{
    SqlExec::run(db,
                 QStringLiteral("UPDATE jobs SET progress_current = ?, progress_total = ?, "
                                "progress_label = ?, heartbeat_at = ? WHERE id = ?"),
                 {current, total, SqlExec::textOrNull(label.left(255)), DbTime::now(), jobId});
}

void heartbeat(QSqlDatabase &db, const QList<qint64> &jobIds)
{
    const QString now = DbTime::now();
    for (const qint64 id : jobIds) {
        SqlExec::run(db, QStringLiteral("UPDATE jobs SET heartbeat_at = ? WHERE id = ?"),
                     {now, id});
    }
}

void finish(QSqlDatabase &db, qint64 jobId, const QString &status, int exitCode,
            const QString &error, const QJsonObject &result)
{
    SqlExec::run(
        db,
        QStringLiteral("UPDATE jobs SET status = ?, exit_code = ?, error = ?, result_json = ?, "
                       "finished_at = ? WHERE id = ?"),
        {status, exitCode, SqlExec::textOrNull(error.left(2000)),
         result.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(toJson(result)),
         DbTime::now(), jobId});
}

bool cancelQueued(QSqlDatabase &db, qint64 jobId)
{
    QSqlQuery query = SqlExec::run(
        db,
        QStringLiteral(
            "UPDATE jobs SET status = 'canceled', finished_at = ? WHERE id = ? AND status "
            "= 'queued'"),
        {DbTime::now(), jobId});
    return query.numRowsAffected() > 0;
}

int failInterrupted(QSqlDatabase &db)
{
    QSqlQuery query = SqlExec::run(
        db,
        QStringLiteral(
            "UPDATE jobs SET status = 'failed', error = ?, finished_at = ? WHERE status = "
            "'running'"),
        {QObject::tr("The app closed while this job was running. Retry it."), DbTime::now()});
    return query.numRowsAffected();
}

qint64 retry(QSqlDatabase &db, qint64 jobId)
{
    QSqlQuery source = SqlExec::run(
        db, QStringLiteral("SELECT type, site_id, params_json, status FROM jobs WHERE id = ?"),
        {jobId});
    if (!source.next() || !isFinalStatus(source.value(3).toString())) {
        return 0;
    }
    const CreateResult created = create(db, source.value(0).toString(),
                                        source.value(1).toLongLong(), fromJson(source.value(2)));
    return created.jobId;
}

std::vector<JobRow> recent(QSqlDatabase &db, int limit)
{
    std::vector<JobRow> rows;
    QSqlQuery query = SqlExec::run(
        db,
        QStringLiteral("SELECT j.id, j.type, j.site_id, s.site_key, j.status, j.progress_current, "
                       "j.progress_total, j.progress_label, j.error, j.created_at, j.started_at, "
                       "j.finished_at, "
                       "j.exit_code FROM jobs j LEFT JOIN sites s ON s.id = j.site_id "
                       "ORDER BY CASE WHEN j.status IN ('running','queued') THEN 0 ELSE 1 END, "
                       "j.id DESC LIMIT %1")
            .arg(limit));
    while (query.next()) {
        JobRow row;
        row.id = query.value(0).toLongLong();
        row.type = query.value(1).toString();
        row.siteId = query.value(2).toLongLong();
        row.siteKey = query.value(3).toString();
        row.status = query.value(4).toString();
        row.progressCurrent = query.value(5).toInt();
        row.progressTotal = query.value(6).toInt();
        row.progressLabel = query.value(7).toString();
        row.error = query.value(8).toString();
        row.createdAt = DbTime::toIso(query.value(9));
        row.startedAt = DbTime::toIso(query.value(10));
        row.finishedAt = DbTime::toIso(query.value(11));
        row.hasExitCode = !query.value(12).isNull();
        row.exitCode = query.value(12).toInt();
        rows.push_back(row);
    }
    return rows;
}

bool isFinalStatus(const QString &status)
{
    return status == QStringLiteral("succeeded") || status == QStringLiteral("partial") ||
           status == QStringLiteral("failed") || status == QStringLiteral("canceled");
}

} // namespace JobStore
