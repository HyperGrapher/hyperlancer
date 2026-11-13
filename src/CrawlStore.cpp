#include "CrawlStore.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include "DbTime.h"
#include "SqlExec.h"

namespace CrawlStore {

namespace {

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

IngestResult ingest(QSqlDatabase &db, const QString &dataDir, qint64 siteId, qint64 jobId,
                    const QString &manifestPath)
{
    IngestResult result;
    const QJsonObject manifest = readJson(manifestPath);
    if (manifest.isEmpty()) {
        result.error = QObject::tr("The crawl wrote no manifest.");
        return result;
    }
    const QJsonObject counts = manifest.value(QStringLiteral("counts")).toObject();
    const QDir root(dataDir);
    const QString relativePath = root.relativeFilePath(QFileInfo(manifestPath).absolutePath());

    // Only the newest crawl is kept.
    removeFiles(db, dataDir, siteId);
    SqlExec::run(db, QStringLiteral("DELETE FROM crawls WHERE site_id = ?"), {siteId});

    QSqlQuery insert = SqlExec::run(
        db,
        QStringLiteral("INSERT INTO crawls (site_id, job_id, status, stop_reason, pages, images, "
                       "files, bytes, errors, path, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
        {siteId, jobId, manifest.value(QStringLiteral("status")).toString(QStringLiteral("failed")),
         SqlExec::textOrNull(manifest.value(QStringLiteral("stop_reason")).toString()),
         counts.value(QStringLiteral("pages")).toInt(), counts.value(QStringLiteral("images")).toInt(),
         counts.value(QStringLiteral("files")).toInt(),
         static_cast<qint64>(counts.value(QStringLiteral("bytes")).toDouble()),
         counts.value(QStringLiteral("errors")).toInt(), relativePath, DbTime::now()},
        &result.error);
    if (result.error.isEmpty()) {
        result.crawlId = insert.lastInsertId().toLongLong();
    }
    return result;
}

QVariantMap latest(QSqlDatabase &db, qint64 siteId)
{
    QSqlQuery query = SqlExec::run(
        db,
        QStringLiteral("SELECT id, job_id, status, stop_reason, pages, images, files, bytes, errors, "
                       "path, created_at FROM crawls WHERE site_id = ? ORDER BY id DESC LIMIT 1"),
        {siteId});
    if (!query.next()) {
        return {};
    }
    return {{QStringLiteral("id"), query.value(0)},
            {QStringLiteral("jobId"), query.value(1)},
            {QStringLiteral("status"), query.value(2)},
            {QStringLiteral("stopReason"), query.value(3).toString()},
            {QStringLiteral("pages"), query.value(4)},
            {QStringLiteral("images"), query.value(5)},
            {QStringLiteral("files"), query.value(6)},
            {QStringLiteral("bytes"), query.value(7)},
            {QStringLiteral("errors"), query.value(8)},
            {QStringLiteral("path"), query.value(9)},
            {QStringLiteral("createdAt"), DbTime::toIso(query.value(10))}};
}

QVariantList images(const QString &dataDir, const QString &relativePath)
{
    QVariantList list;
    const QString folder = QDir(dataDir).filePath(relativePath);
    const QJsonArray assets = readJson(QDir(folder).filePath(QStringLiteral("assets.json")))
                                  .value(QStringLiteral("assets"))
                                  .toArray();
    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        if (asset.value(QStringLiteral("kind")).toString() != QStringLiteral("image")) {
            continue;
        }
        const QJsonArray alts = asset.value(QStringLiteral("alt_texts")).toArray();
        const QString file = QDir(folder).filePath(asset.value(QStringLiteral("file")).toString());
        list.append(QVariantMap{
            {QStringLiteral("file"), file},
            {QStringLiteral("source"), QUrl::fromLocalFile(file)},
            {QStringLiteral("url"), asset.value(QStringLiteral("url")).toString()},
            {QStringLiteral("alt"), alts.isEmpty() ? QString() : alts.first().toString()},
            {QStringLiteral("width"), asset.value(QStringLiteral("width")).toInt()},
            {QStringLiteral("height"), asset.value(QStringLiteral("height")).toInt()},
            {QStringLiteral("bytes"), static_cast<qint64>(asset.value(QStringLiteral("bytes")).toDouble())},
            {QStringLiteral("pageIds"), asset.value(QStringLiteral("used_on")).toVariant()}});
    }
    return list;
}

QVariantList pages(const QString &dataDir, const QString &relativePath)
{
    QVariantList list;
    const QString folder = QDir(dataDir).filePath(relativePath);
    QHash<QString, int> imageCounts;
    for (const QVariant &image : images(dataDir, relativePath)) {
        for (const QVariant &pageId : image.toMap().value(QStringLiteral("pageIds")).toList()) {
            ++imageCounts[pageId.toString()];
        }
    }
    const QJsonArray entries = readJson(QDir(folder).filePath(QStringLiteral("structure.json")))
                                   .value(QStringLiteral("pages"))
                                   .toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject page = value.toObject();
        const QString id = page.value(QStringLiteral("id")).toString();
        list.append(QVariantMap{{QStringLiteral("id"), id},
                                {QStringLiteral("imageCount"), imageCounts.value(id)},
                                {QStringLiteral("title"), page.value(QStringLiteral("title")).toString()},
                                {QStringLiteral("url"), page.value(QStringLiteral("url")).toString()},
                                {QStringLiteral("file"), QDir(folder).filePath(page.value(QStringLiteral("file")).toString())},
                                {QStringLiteral("wordCount"), page.value(QStringLiteral("word_count")).toInt()},
                                {QStringLiteral("depth"), page.value(QStringLiteral("depth")).toInt()},
                                {QStringLiteral("lang"), page.value(QStringLiteral("lang")).toString()}});
    }
    return list;
}

void removeFiles(QSqlDatabase &db, const QString &dataDir, qint64 siteId)
{
    QSqlQuery query = SqlExec::run(db, QStringLiteral("SELECT path FROM crawls WHERE site_id = ?"), {siteId});
    while (query.next()) {
        QDir(QDir(dataDir).filePath(query.value(0).toString())).removeRecursively();
    }
}

} // namespace CrawlStore
