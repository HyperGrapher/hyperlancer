#include "ProfileIngest.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "DbTime.h"
#include "SiteStore.h"
#include "SqlExec.h"

namespace ProfileIngest {

namespace {

QVariant jsonValueToVariant(const QJsonValue &value)
{
    if (value.isNull() || value.isUndefined()) {
        return {};
    }
    if (value.isDouble()) {
        return static_cast<qint64>(value.toDouble());
    }
    return value.toVariant();
}

QString compact(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

// The site columns that summary.facts may set. Anything else in facts is ignored, which is
// what makes it safe to build the UPDATE from the list.
const QStringList &factColumns()
{
    static const QStringList columns{
        QStringLiteral("http_status"),     QStringLiteral("final_url"),
        QStringLiteral("title"),           QStringLiteral("cms"),
        QStringLiteral("cms_version"),     QStringLiteral("server"),
        QStringLiteral("hosting"),         QStringLiteral("backend_language"),
        QStringLiteral("ip_address"),      QStringLiteral("email_provider"),
        QStringLiteral("tls_valid_until"), QStringLiteral("domain_expires_on"),
        QStringLiteral("registrar"),       QStringLiteral("favicon_url"),
    };
    return columns;
}

QJsonObject failedSummary(const QString &message)
{
    QJsonObject error{{QStringLiteral("section"), QStringLiteral("job")},
                      {QStringLiteral("code"), QStringLiteral("crash")},
                      {QStringLiteral("message"), message}};
    return QJsonObject{{QStringLiteral("summaryVersion"), 1},
                       {QStringLiteral("outcome"), QStringLiteral("failed")},
                       {QStringLiteral("errors"), QJsonArray{error}},
                       {QStringLiteral("facts"), QJsonObject()}};
}

QString shortError(const QJsonObject &summary)
{
    const QJsonArray errors = summary.value(QStringLiteral("errors")).toArray();
    if (errors.isEmpty()) {
        return {};
    }
    const QJsonObject first = errors.first().toObject();
    const QString section = first.value(QStringLiteral("section")).toString();
    const QString message = first.value(QStringLiteral("message")).toString();
    return (section.isEmpty() ? message : section + QStringLiteral(": ") + message).left(500);
}

Result store(QSqlDatabase &db, qint64 siteId, qint64 jobId, QJsonObject raw, QJsonObject summary)
{
    Result result;
    raw.remove(QStringLiteral("summary"));
    const QString outcomeValue = summary.value(QStringLiteral("outcome")).toString();
    result.outcome =
        (outcomeValue == QStringLiteral("ok") || outcomeValue == QStringLiteral("partial"))
            ? outcomeValue
            : QStringLiteral("failed");

    QSqlQuery site =
        SqlExec::run(db, QStringLiteral("SELECT url FROM sites WHERE id = ?"), {siteId});
    if (!site.next()) {
        result.error = QObject::tr("The site was deleted before its analysis finished.");
        return result;
    }
    const QString siteUrl = site.value(0).toString();

    const QJsonObject metadata = raw.value(QStringLiteral("metadata")).toObject();
    const QJsonObject urls = summary.value(QStringLiteral("url")).toObject();
    const QJsonObject http = summary.value(QStringLiteral("http")).toObject();
    QDateTime fetched = QDateTime::fromString(
        metadata.value(QStringLiteral("scanned_at_utc")).toString(), Qt::ISODate);
    if (!fetched.isValid()) {
        fetched = QDateTime::currentDateTimeUtc();
    }
    const QString fetchedAt = DbTime::toDb(fetched);
    const QString requested = urls.value(QStringLiteral("requested")).toString();
    const QString errorSummary = shortError(summary);

    SqlExec::Transaction transaction(db);
    QString error;
    QSqlQuery insert = SqlExec::run(
        db,
        QStringLiteral(
            "INSERT INTO profile_snapshots (site_id, job_id, fetched_at, outcome, schema_version, "
            "tool_version, requested_url, final_url, http_status, duration_ms, error_summary, "
            "summary_json, raw_json, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
            "?)"),
        {siteId, jobId > 0 ? QVariant(jobId) : SqlExec::nullInt(), fetchedAt, result.outcome,
         raw.value(QStringLiteral("schema_version")).toInt(2),
         SqlExec::textOrNull(metadata.value(QStringLiteral("version")).toString()),
         requested.isEmpty() ? siteUrl : requested,
         SqlExec::textOrNull(urls.value(QStringLiteral("final")).toString()),
         jsonValueToVariant(http.value(QStringLiteral("status"))),
         jsonValueToVariant(metadata.value(QStringLiteral("duration_ms"))),
         SqlExec::textOrNull(errorSummary), compact(summary), compact(raw), DbTime::now()},
        &error);
    if (!error.isEmpty()) {
        result.error = error;
        return result;
    }
    result.snapshotId = insert.lastInsertId().toLongLong();

    const QString techSql = SqlExec::insertIgnore(db) +
                            QStringLiteral(" INTO snapshot_technologies (snapshot_id, category, "
                                           "name, version, confidence) VALUES (?, ?, ?, ?, ?)");
    for (const QJsonValue &value : summary.value(QStringLiteral("tech")).toArray()) {
        const QJsonObject tech = value.toObject();
        SqlExec::run(
            db, techSql,
            {result.snapshotId, tech.value(QStringLiteral("category")).toString().left(100),
             tech.value(QStringLiteral("name")).toString().left(150),
             SqlExec::textOrNull(tech.value(QStringLiteral("version")).toString().left(60)),
             tech.value(QStringLiteral("confidence")).toString(QStringLiteral("low"))});
    }

    const QString now = DbTime::now();
    if (result.outcome == QStringLiteral("failed")) {
        // A failed attempt must not blank the profile: latest_snapshot_id keeps pointing at the
        // newest snapshot with data, unless there is none yet.
        SqlExec::run(
            db,
            QStringLiteral("UPDATE sites SET last_fetched_at = ?, last_fetch_outcome = "
                           "'failed', latest_snapshot_id = COALESCE(latest_snapshot_id, ?), "
                           "updated_at = ? WHERE id = ?"),
            {fetchedAt, result.snapshotId, now, siteId}, &error);
    } else {
        const QJsonObject facts = summary.value(QStringLiteral("facts")).toObject();
        QStringList assignments;
        QVariantList binds;
        for (const QString &column : factColumns()) {
            assignments.append(column + QStringLiteral(" = ?"));
            binds.append(jsonValueToVariant(facts.value(column)));
        }
        QSqlQuery update(db);
        update.prepare(
            QStringLiteral("UPDATE sites SET %1, latest_snapshot_id = ?, last_fetched_at = "
                           "?, last_fetch_outcome = ?, updated_at = ? WHERE id = ?")
                .arg(assignments.join(QStringLiteral(", "))));
        for (const QVariant &bind : binds) {
            update.addBindValue(bind);
        }
        update.addBindValue(result.snapshotId);
        update.addBindValue(fetchedAt);
        update.addBindValue(result.outcome);
        update.addBindValue(now);
        update.addBindValue(siteId);
        if (!update.exec()) {
            error = update.lastError().text();
        }
    }
    if (!error.isEmpty()) {
        result.error = error;
        return result;
    }

    const QString message =
        result.outcome == QStringLiteral("failed")
            ? QObject::tr("Analysis failed%1")
                  .arg(errorSummary.isEmpty() ? QString() : QStringLiteral(": ") + errorSummary)
            : QObject::tr("Analysis finished (%1)").arg(result.outcome);
    SiteStore::logActivity(db, siteId, QStringLiteral("profile.fetched"), message,
                           {{QStringLiteral("snapshotId"), result.snapshotId},
                            {QStringLiteral("outcome"), result.outcome}});
    if (!transaction.commit()) {
        result.error = db.lastError().text();
        result.snapshotId = 0;
    }
    return result;
}

} // namespace

Result ingestFile(QSqlDatabase &db, qint64 siteId, qint64 jobId, const QString &reportPath,
                  const QString &fallbackMessage)
{
    QFile file(reportPath);
    if (reportPath.isEmpty() || !file.exists()) {
        return ingestFailure(db, siteId, jobId, fallbackMessage);
    }
    if (file.size() > kMaxReportBytes) {
        return ingestFailure(
            db, siteId, jobId,
            QObject::tr("The report is too large (%1 MB).").arg(file.size() >> 20));
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return ingestFailure(db, siteId, jobId,
                             QObject::tr("Could not read the report: %1").arg(file.errorString()));
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return ingestFailure(
            db, siteId, jobId,
            QObject::tr("The report is not valid JSON: %1").arg(parseError.errorString()));
    }
    const QJsonObject raw = document.object();
    const QJsonObject summary = raw.value(QStringLiteral("summary")).toObject();
    if (summary.isEmpty()) {
        return ingestFailure(db, siteId, jobId,
                             QObject::tr("The report has no summary. Update the Python tools."));
    }
    return store(db, siteId, jobId, raw, summary);
}

Result ingestFailure(QSqlDatabase &db, qint64 siteId, qint64 jobId, const QString &message)
{
    QJsonObject raw{{QStringLiteral("schema_version"), 2},
                    {QStringLiteral("status"), QStringLiteral("failed")},
                    {QStringLiteral("metadata"),
                     QJsonObject{{QStringLiteral("scanned_at_utc"),
                                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}}}};
    return store(db, siteId, jobId, raw, failedSummary(message));
}

} // namespace ProfileIngest
