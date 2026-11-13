#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Content crawls of a website (the site_crawl job): what the last one found and where its
// files are. The crawl folder itself lives in the data folder; the database only holds the
// summary and the relative path.
namespace CrawlStore {

struct IngestResult
{
    qint64 crawlId = 0;
    QString error;
};

// Reads manifest.json of a finished crawl and stores its summary. Older crawls of the same
// website are deleted (rows and folders): only the latest one is kept.
[[nodiscard]] IngestResult ingest(QSqlDatabase &db, const QString &dataDir, qint64 siteId,
                                  qint64 jobId, const QString &manifestPath);

// The latest crawl: { id, jobId, status, stopReason, pages, images, files, bytes, errors,
// path, createdAt }; empty when the website was never crawled.
[[nodiscard]] QVariantMap latest(QSqlDatabase &db, qint64 siteId);

// The pages of a crawl folder (structure.json): { id, title, url, file, wordCount, depth, lang,
// imageCount }.
[[nodiscard]] QVariantList pages(const QString &dataDir, const QString &relativePath);

// The images of a crawl folder (assets.json): { file (absolute path), url (where it came from),
// alt, width, height, bytes, pageIds, source (file URL for an Image) }. The pages list tells how many images each page has.
[[nodiscard]] QVariantList images(const QString &dataDir, const QString &relativePath);

// Removes every crawl folder of a website, for when the website is deleted.
void removeFiles(QSqlDatabase &db, const QString &dataDir, qint64 siteId);

} // namespace CrawlStore
