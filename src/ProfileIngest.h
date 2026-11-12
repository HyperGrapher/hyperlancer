#pragma once

#include <QSqlDatabase>
#include <QString>

// Stores a site_profile report as a snapshot and copies its facts onto the site, in one
// transaction. A snapshot is stored for every attempt, including failed ones, so the history
// shows what happened.
namespace ProfileIngest {

struct Result
{
    qint64 snapshotId = 0;
    QString outcome; // ok, partial, failed
    QString error;   // set when nothing could be stored
};

// Reports larger than this are refused instead of loaded.
inline constexpr qint64 kMaxReportBytes = 32 * 1024 * 1024;

// reportPath is the profile.json the tool wrote. When it is missing or unreadable, a failed
// snapshot is stored with fallbackMessage.
[[nodiscard]] Result ingestFile(QSqlDatabase &db, qint64 siteId, qint64 jobId,
                                const QString &reportPath, const QString &fallbackMessage);
[[nodiscard]] Result ingestFailure(QSqlDatabase &db, qint64 siteId, qint64 jobId,
                                   const QString &message);

} // namespace ProfileIngest
