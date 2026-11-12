#include "Discovery.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSqlQuery>

#include "Database.h"
#include "DbTime.h"
#include "Integrations.h"
#include "JobRunner.h"
#include "LangSearchClient.h"
#include "PublicSuffixList.h"
#include "SiteStore.h"
#include "SqlExec.h"
#include "UrlNormalizer.h"

namespace {

// Sites that are never leads: search engines, social networks, marketplaces, directories.
// They are dropped from results so the list shows businesses' own websites.
const QSet<QString> &ignoredDomains()
{
    static const QSet<QString> domains{
        QStringLiteral("google.com"),
        QStringLiteral("youtube.com"),
        QStringLiteral("facebook.com"),
        QStringLiteral("instagram.com"),
        QStringLiteral("linkedin.com"),
        QStringLiteral("twitter.com"),
        QStringLiteral("x.com"),
        QStringLiteral("tiktok.com"),
        QStringLiteral("wikipedia.org"),
        QStringLiteral("pinterest.com"),
        QStringLiteral("reddit.com"),
        QStringLiteral("sahibinden.com"),
        QStringLiteral("trendyol.com"),
        QStringLiteral("hepsiburada.com"),
        QStringLiteral("n11.com"),
        QStringLiteral("amazon.com"),
        QStringLiteral("amazon.com.tr"),
        QStringLiteral("yandex.com.tr"),
        QStringLiteral("tripadvisor.com"),
        QStringLiteral("tripadvisor.com.tr"),
        QStringLiteral("yelp.com"),
        QStringLiteral("doktortakvimi.com"),
        QStringLiteral("eksisozluk.com"),
        QStringLiteral("medium.com"),
        QStringLiteral("tavsiyeediyorum.com"),
        QStringLiteral("sikayetvar.com"),
        QStringLiteral("foursquare.com"),
    };
    return domains;
}

// Throwaway blog hosts ("darren950454.example-blogs.com") fill search results with spam: a
// subdomain whose first label carries five or more digits is almost never a business site.
bool looksLikeBlogFarm(const QString &host, const QString &registrableDomain)
{
    static const QRegularExpression manyDigits(QStringLiteral("\\d{5,}"));
    if (registrableDomain.isEmpty() || host == registrableDomain ||
        host == QStringLiteral("www.") + registrableDomain) {
        return false;
    }
    return manyDigits.match(host.section(QChar(u'.'), 0, 0)).hasMatch();
}

struct SearchStored
{
    qint64 runId = 0;
    QSet<QString> known;
};

} // namespace

Discovery::Discovery(Database &database, JobRunner &runner, LangSearchClient &client,
                     Integrations &integrations, QObject *parent)
    : QAbstractListModel(parent)
    , m_database(database)
    , m_runner(runner)
    , m_client(client)
    , m_integrations(integrations)
{}

int Discovery::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant Discovery::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Row &row = m_rows.at(static_cast<size_t>(index.row()));
    switch (role) {
    case UrlRole:
        return row.url;
    case DomainRole:
        return row.siteKey;
    case TitleRole:
        return row.title;
    case SnippetRole:
        return row.snippet;
    case KnownRole:
        return row.known;
    case SelectedRole:
        return row.selected;
    case ImportedRole:
        return row.imported;
    case DuplicateRole:
        return row.duplicate;
    default:
        return {};
    }
}

QHash<int, QByteArray> Discovery::roleNames() const
{
    return {{UrlRole, "url"},           {DomainRole, "domain"},      {TitleRole, "title"},
            {SnippetRole, "snippet"},   {KnownRole, "known"},        {SelectedRole, "selected"},
            {ImportedRole, "imported"}, {DuplicateRole, "duplicate"}};
}

int Discovery::newCount() const
{
    int count = 0;
    for (const Row &row : m_rows) {
        if (!row.known && !row.duplicate && !row.imported) {
            ++count;
        }
    }
    return count;
}

int Discovery::selectedCount() const
{
    int count = 0;
    for (const Row &row : m_rows) {
        if (row.selected) {
            ++count;
        }
    }
    return count;
}

void Discovery::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void Discovery::search(const QString &query, int count, const QString &freshness)
{
    if (m_busy) {
        return;
    }
    LangSearch::Request request;
    request.query = query.trimmed();
    request.count = count;
    request.freshness = freshness;
    request.excludeDomains = QStringList(ignoredDomains().cbegin(), ignoredDomains().cend());
    setBusy(true);
    m_client.search(
        m_integrations.langSearchKey(), request,
        [this, request](const LangSearch::Response &response) {
            if (!response.error.isEmpty()) {
                setBusy(false);
                showRows({}, request.query, response.error);
                emit searchFinished(false);
                return;
            }
            // One row per website: search results often hold several pages of one site.
            std::vector<Row> rows;
            QSet<QString> seen;
            const PublicSuffixList &suffixes = PublicSuffixList::bundled();
            for (const LangSearch::Hit &hit : response.hits) {
                const UrlNormalizeResult normalized = normalizeSiteRoot(hit.url, suffixes);
                if (!normalized.ok()) {
                    continue;
                }
                const QString domain = normalized.value.registrableDomain.isEmpty()
                                           ? normalized.value.siteKey
                                           : normalized.value.registrableDomain;
                if (ignoredDomains().contains(domain) ||
                    looksLikeBlogFarm(normalized.value.host, normalized.value.registrableDomain)) {
                    continue;
                }
                Row row;
                row.url = normalized.value.url;
                row.domain = domain;
                row.siteKey = normalized.value.siteKey;
                row.title = hit.name.simplified();
                row.snippet = hit.snippet.simplified();
                row.duplicate = seen.contains(domain);
                seen.insert(domain);
                rows.push_back(row);
            }

            QStringList domains;
            for (const Row &row : rows) {
                domains.append(row.domain);
            }
            const QJsonObject params{{QStringLiteral("count"), request.count},
                                     {QStringLiteral("freshness"), request.freshness}};
            const int units = std::max(1, response.inputTokens + response.outputTokens);
            const std::vector<Row> found = rows;
            m_database.run(
                this,
                [request, params, found, domains, units](QSqlDatabase &db) {
                    SearchStored stored;
                    stored.known = SiteStore::knownDomains(db, domains);
                    int newCount = 0;
                    for (const Row &row : found) {
                        if (!row.duplicate && !stored.known.contains(row.domain)) {
                            ++newCount;
                        }
                    }
                    QSqlQuery run = SqlExec::run(
                        db,
                        QStringLiteral(
                            "INSERT INTO discovery_runs (source, query, params_json, result_count, "
                            "new_count, api_units, created_at) VALUES ('langsearch', ?, ?, ?, ?, "
                            "?, ?)"),
                        {request.query.left(1000),
                         QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact)),
                         static_cast<int>(found.size()), newCount, units, DbTime::now()});
                    stored.runId = run.lastInsertId().toLongLong();
                    int position = 0;
                    for (const Row &row : found) {
                        SqlExec::run(
                            db,
                            QStringLiteral("INSERT INTO discovery_results (run_id, position, url, "
                                           "title, snippet) VALUES (?, ?, ?, ?, ?)"),
                            {stored.runId, position++, row.url, row.title.left(1000), row.snippet});
                    }
                    SiteStore::addApiUsage(db, QStringLiteral("langsearch"), 1);
                    return stored;
                },
                [this, rows = std::move(rows),
                 query = request.query](const SearchStored &stored) mutable {
                    for (Row &row : rows) {
                        row.known =
                            stored.known.contains(row.domain) || stored.known.contains(row.siteKey);
                        row.selected = !row.known && !row.duplicate;
                    }
                    m_runId = stored.runId;
                    setBusy(false);
                    showRows(std::move(rows), query, QString());
                    loadRecentRuns();
                    emit searchFinished(true);
                });
        });
}

void Discovery::showRows(std::vector<Row> rows, const QString &query, const QString &error)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    m_lastQuery = query;
    m_error = error;
    emit resultsChanged();
    emit selectionChanged();
}

void Discovery::toggle(int row)
{
    if (row < 0 || row >= rowCount()) {
        return;
    }
    Row &item = m_rows[static_cast<size_t>(row)];
    if (item.imported || item.known) {
        return;
    }
    item.selected = !item.selected;
    const QModelIndex changed = index(row);
    emit dataChanged(changed, changed, {SelectedRole});
    emit selectionChanged();
}

void Discovery::selectAllNew()
{
    for (Row &row : m_rows) {
        row.selected = !row.known && !row.duplicate && !row.imported;
    }
    if (!m_rows.empty()) {
        emit dataChanged(index(0), index(rowCount() - 1), {SelectedRole});
    }
    emit selectionChanged();
}

void Discovery::clearSelection()
{
    for (Row &row : m_rows) {
        row.selected = false;
    }
    if (!m_rows.empty()) {
        emit dataChanged(index(0), index(rowCount() - 1), {SelectedRole});
    }
    emit selectionChanged();
}

void Discovery::importSelected(bool analyze)
{
    std::vector<std::pair<int, QString>> chosen;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].selected) {
            chosen.emplace_back(static_cast<int>(i), m_rows[i].url);
        }
    }
    if (chosen.empty()) {
        return;
    }
    const qint64 runId = m_runId;
    struct Imported
    {
        int row = -1;
        qint64 siteId = 0;
        QString url;
        bool created = false;
    };
    std::vector<std::pair<int, QString>> titles;
    for (const auto &[row, url] : chosen) {
        titles.emplace_back(row, m_rows[static_cast<size_t>(row)].title);
    }
    m_database.run(
        this,
        [chosen, titles, runId](QSqlDatabase &db) {
            std::vector<Imported> imported;
            const PublicSuffixList &suffixes = PublicSuffixList::bundled();
            for (size_t i = 0; i < chosen.size(); ++i) {
                const UrlNormalizeResult normalized = normalizeSiteRoot(chosen[i].second, suffixes);
                if (!normalized.ok()) {
                    continue;
                }
                const SiteStore::AddResult added = SiteStore::addSite(
                    db, normalized.value, QStringLiteral("discovery"), titles[i].second);
                if (added.siteId <= 0) {
                    continue;
                }
                if (runId > 0) {
                    SqlExec::run(
                        db,
                        QStringLiteral("UPDATE discovery_results SET site_id = ? WHERE run_id = ? "
                                       "AND url = ?"),
                        {added.siteId, runId, chosen[i].second});
                }
                imported.push_back(
                    {chosen[i].first, added.siteId, normalized.value.url, added.created});
            }
            return imported;
        },
        [this, analyze](const std::vector<Imported> &imported) {
            int created = 0;
            for (const Imported &item : imported) {
                if (item.row < rowCount()) {
                    Row &row = m_rows[static_cast<size_t>(item.row)];
                    row.imported = true;
                    row.selected = false;
                    const QModelIndex changed = index(item.row);
                    emit dataChanged(changed, changed, {ImportedRole, SelectedRole});
                }
                if (item.created) {
                    ++created;
                }
                if (analyze) {
                    m_runner.enqueueProfile(item.siteId, item.url);
                }
            }
            emit selectionChanged();
            emit resultsChanged();
            emit notice(analyze ? tr("%n site(s) added and queued for analysis.", nullptr, created)
                                : tr("%n site(s) added.", nullptr, created),
                        false);
        });
}

void Discovery::loadRecentRuns()
{
    m_database.run(
        this,
        [](QSqlDatabase &db) {
            QVariantList runs;
            QSqlQuery query = SqlExec::run(
                db, QStringLiteral("SELECT id, query, result_count, new_count, created_at FROM "
                                   "discovery_runs ORDER BY id DESC LIMIT 12"));
            while (query.next()) {
                runs.append(
                    QVariantMap{{QStringLiteral("id"), query.value(0)},
                                {QStringLiteral("query"), query.value(1)},
                                {QStringLiteral("resultCount"), query.value(2)},
                                {QStringLiteral("newCount"), query.value(3)},
                                {QStringLiteral("createdAt"), DbTime::toIso(query.value(4))}});
            }
            return runs;
        },
        [this](const QVariantList &runs) {
            m_recentRuns = runs;
            emit recentRunsChanged();
        });
}

void Discovery::openRun(qint64 runId)
{
    struct Stored
    {
        QString query;
        std::vector<Row> rows;
        QSet<QString> known;
    };
    m_database.run(
        this,
        [runId](QSqlDatabase &db) {
            Stored stored;
            QSqlQuery run = SqlExec::run(
                db, QStringLiteral("SELECT query FROM discovery_runs WHERE id = ?"), {runId});
            if (run.next()) {
                stored.query = run.value(0).toString();
            }
            QSqlQuery results = SqlExec::run(
                db,
                QStringLiteral(
                    "SELECT url, title, snippet, site_id FROM discovery_results WHERE run_id = ? "
                    "ORDER BY position"),
                {runId});
            QSet<QString> seen;
            QStringList domains;
            const PublicSuffixList &suffixes = PublicSuffixList::bundled();
            while (results.next()) {
                const UrlNormalizeResult normalized =
                    normalizeSiteRoot(results.value(0).toString(), suffixes);
                if (!normalized.ok() ||
                    looksLikeBlogFarm(normalized.value.host, normalized.value.registrableDomain)) {
                    continue;
                }
                Row row;
                row.url = normalized.value.url;
                row.siteKey = normalized.value.siteKey;
                row.domain = normalized.value.registrableDomain.isEmpty()
                                 ? row.siteKey
                                 : normalized.value.registrableDomain;
                row.title = results.value(1).toString();
                row.snippet = results.value(2).toString();
                row.imported = !results.value(3).isNull();
                row.duplicate = seen.contains(row.domain);
                seen.insert(row.domain);
                domains.append(row.domain);
                stored.rows.push_back(row);
            }
            stored.known = SiteStore::knownDomains(db, domains);
            return stored;
        },
        [this, runId](Stored stored) {
            for (Row &row : stored.rows) {
                row.known = !row.imported && (stored.known.contains(row.domain) ||
                                              stored.known.contains(row.siteKey));
                row.selected = !row.known && !row.duplicate && !row.imported;
            }
            m_runId = runId;
            showRows(std::move(stored.rows), stored.query, QString());
        });
}
