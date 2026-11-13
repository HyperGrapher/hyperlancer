#include "RenderStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "DbTime.h"
#include "SiteStore.h"
#include "SqlExec.h"

namespace RenderStore {

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

IngestResult ingest(QSqlDatabase &db, const QString &dataDir, qint64 siteId, qint64 jobId,
                    const QString &renderJsonPath)
{
    IngestResult result;
    const QJsonObject report = QJsonDocument::fromJson(readFile(renderJsonPath)).object();
    if (report.isEmpty()) {
        result.error = QObject::tr("The browser render wrote no report.");
        return result;
    }
    const QDir folder = QFileInfo(renderJsonPath).absoluteDir();
    const QJsonObject files = report.value(QStringLiteral("files")).toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        SiteStore::saveScreenshot(db, dataDir, siteId, it.key(), QStringLiteral("render"),
                                  readFile(folder.filePath(it.value().toString())));
    }

    SqlExec::run(db, QStringLiteral("DELETE FROM renders WHERE site_id = ?"), {siteId});
    const QJsonDocument trackers(report.value(QStringLiteral("trackers")).toObject());
    SqlExec::run(db,
                 QStringLiteral("INSERT INTO renders (site_id, job_id, title, load_ms, request_count, "
                                "trackers_json, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)"),
                 {siteId, jobId, report.value(QStringLiteral("title")).toString().left(500),
                  report.value(QStringLiteral("loadMs")).toInt(),
                  report.value(QStringLiteral("requestCount")).toInt(),
                  QString::fromUtf8(trackers.toJson(QJsonDocument::Compact)), DbTime::now()},
                 &result.error);
    QDir(folder).removeRecursively(); // the screenshots are in the data folder now
    return result;
}

QVariantMap latest(QSqlDatabase &db, qint64 siteId)
{
    QSqlQuery query = SqlExec::run(
        db,
        QStringLiteral("SELECT title, load_ms, request_count, trackers_json, created_at FROM renders "
                       "WHERE site_id = ? ORDER BY id DESC LIMIT 1"),
        {siteId});
    if (!query.next()) {
        return {};
    }
    return {{QStringLiteral("title"), query.value(0)},
            {QStringLiteral("loadMs"), query.value(1)},
            {QStringLiteral("requestCount"), query.value(2)},
            {QStringLiteral("trackers"),
             QJsonDocument::fromJson(query.value(3).toString().toUtf8()).object().toVariantMap()},
            {QStringLiteral("createdAt"), query.value(4)}};
}

} // namespace RenderStore
