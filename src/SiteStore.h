#pragma once

#include <vector>

#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "UrlNormalizer.h"

// Reads and writes of sites and their activity. Plain functions over a connection: they run
// on the database thread (see Database::run) and never touch the UI.
namespace SiteStore {

struct AddResult
{
    qint64 siteId = 0;
    bool created = false;
    QString error;
};

// One row of the sites list. Only what the list shows and sorts on.
struct SiteRow
{
    qint64 id = 0;
    QString siteKey;
    QString url;
    QString title;
    QString cms;
    QString hosting;
    QString emailProvider;
    int httpStatus = 0;
    QString outcome;       // "", "ok", "partial", "failed"
    QString lastFetchedAt; // ISO
    QString tlsValidUntil; // YYYY-MM-DD
    QString domainExpiresOn;
    QString source;
    QString activeJobStatus; // "queued" or "running" while a profile job is pending

    friend bool operator==(const SiteRow &, const SiteRow &) = default;
};

enum class Filter
{
    All,
    NeverAnalyzed,
    Failed,
    Expiring
};

struct ListQuery
{
    QString search;
    Filter filter = Filter::All;
    int limit = 5000;
};

struct ListResult
{
    std::vector<SiteRow> rows;
    int total = 0;
    QString error;
};

// Inserts the site, or returns the existing one with the same site key.
[[nodiscard]] AddResult addSite(QSqlDatabase &db, const NormalizedUrl &url, const QString &source,
                                const QString &name = {});
[[nodiscard]] ListResult listSites(QSqlDatabase &db, const ListQuery &query);
// Site row, latest snapshot summary, snapshot history and activity, as maps for QML.
// "error" is set when the site does not exist.
[[nodiscard]] QVariantMap loadDetail(QSqlDatabase &db, qint64 siteId);
[[nodiscard]] QVariantMap loadSnapshot(QSqlDatabase &db, qint64 snapshotId);
// Registrable domains (and site keys) that are already tracked, out of the given ones.
[[nodiscard]] QSet<QString> knownDomains(QSqlDatabase &db, const QStringList &domains);
// Registrable domains of a site, for mail matching.
[[nodiscard]] QStringList siteDomains(QSqlDatabase &db, qint64 siteId);
bool deleteSite(QSqlDatabase &db, qint64 siteId);
bool setPreviewUrl(QSqlDatabase &db, qint64 siteId, const QString &url);

// Stores a PageSpeed Insights result and copies the score onto the site.
void saveSpeedCheck(QSqlDatabase &db, qint64 siteId, int score, const QVariantMap &metrics,
                    const QString &error);

// Counts and short lists for the dashboard: totals, recently analyzed, expiring soon,
// API usage this month.
[[nodiscard]] QVariantMap dashboard(QSqlDatabase &db);

// Adds units to this month's counter for a paid or quota-limited service.
void addApiUsage(QSqlDatabase &db, const QString &service, int units);

void logActivity(QSqlDatabase &db, qint64 siteId, const QString &type, const QString &message,
                 const QVariantMap &data = {});

} // namespace SiteStore
