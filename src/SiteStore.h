#pragma once

#include <vector>

#include <QByteArray>
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
    QString faviconUrl;
    QString deadlineOn; // YYYY-MM-DD
    QString status;   // lead status, see SiteOrganizer::statuses()
    QString category;
    QString tags;     // comma separated names
    int openTodos = 0;
    QString activeJobStatus; // "queued" or "running" while a profile job is pending

    friend bool operator==(const SiteRow &, const SiteRow &) = default;
};

enum class Filter
{
    All,
    Failed,
    Expiring
};

enum class SortKey
{
    Analyzed,
    Site,
    Status,
    Category,
    Deadline,
    Todos
};

struct ListQuery
{
    QString search;
    Filter filter = Filter::All;
    QString status;   // empty: every status
    QString category; // empty: every category
    SortKey sortKey = SortKey::Analyzed;
    bool descending = true;
    int limit = 5000;
};

struct ListResult
{
    std::vector<SiteRow> rows;
    int total = 0;
    QStringList categories; // every category name, for the filter
    QStringList tags;       // every tag name, for the bulk tag picker
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
// What changed in this analysis compared with the one before it (the newest earlier one that
// did not fail). Adds "beforeAt" and "afterAt" to the SnapshotDiff result; "error" says why
// there is nothing to compare.
[[nodiscard]] QVariantMap diffWithPrevious(QSqlDatabase &db, qint64 snapshotId);
// Registrable domains (and site keys) that are already tracked, out of the given ones.
[[nodiscard]] QSet<QString> knownDomains(QSqlDatabase &db, const QStringList &domains);
// Registrable domains of a site, for mail matching.
[[nodiscard]] QStringList siteDomains(QSqlDatabase &db, qint64 siteId);
bool deleteSite(QSqlDatabase &db, qint64 siteId);
bool setPreviewUrl(QSqlDatabase &db, qint64 siteId, const QString &url);

// Stores a PageSpeed Insights result (strategy "mobile" or "desktop"). A mobile score is also
// copied onto the site.
void saveSpeedCheck(QSqlDatabase &db, qint64 siteId, const QString &strategy, int score,
                    const QVariantMap &metrics, const QString &error);

// Writes a screenshot ("pagespeed" or "render" as source) under <dataDir>/screenshots/<siteId>/ and records it; the previous one
// of the same kind is removed. Returns false when the file cannot be written.
bool saveScreenshot(QSqlDatabase &db, const QString &dataDir, qint64 siteId, const QString &kind,
                    const QString &source, const QByteArray &jpeg);

// Counts and short lists for the dashboard: totals, recently analyzed, expiring soon,
// API usage this month.
[[nodiscard]] QVariantMap dashboard(QSqlDatabase &db);

// Saved Websites-list views: a name and the list settings as a JSON object.
[[nodiscard]] QVariantList listSavedViews(QSqlDatabase &db); // { id, name, config }
// Creates the view or, when the name exists, replaces its settings. Returns the id, 0 on failure.
qint64 saveView(QSqlDatabase &db, const QString &name, const QVariantMap &config);
bool deleteSavedView(QSqlDatabase &db, qint64 viewId);

// Adds units to this month's counter for a paid or quota-limited service.
void addApiUsage(QSqlDatabase &db, const QString &service, int units);

void logActivity(QSqlDatabase &db, qint64 siteId, const QString &type, const QString &message,
                 const QVariantMap &data = {});

} // namespace SiteStore
