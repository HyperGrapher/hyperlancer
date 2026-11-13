#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVariantMap>

// The browser render of a website (the site_render job): three screenshots and the trackers
// seen in the requests the page made. Only the newest render is kept.
namespace RenderStore {

struct IngestResult
{
    QString error;
};

// Reads render.json from a finished job, saves its screenshots (replacing the old ones) and
// stores the summary. The job folder with the raw files is removed afterwards.
[[nodiscard]] IngestResult ingest(QSqlDatabase &db, const QString &dataDir, qint64 siteId,
                                  qint64 jobId, const QString &renderJsonPath);

// { title, loadMs, requestCount, trackers, createdAt }; empty when the website was never rendered.
[[nodiscard]] QVariantMap latest(QSqlDatabase &db, qint64 siteId);

} // namespace RenderStore
