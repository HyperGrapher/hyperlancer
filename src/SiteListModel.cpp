#include "SiteListModel.h"

#include <algorithm>

#include "Database.h"
#include "JobRunner.h"

SiteListModel::SiteListModel(Database &database, JobRunner &runner, QObject *parent)
    : QAbstractListModel(parent)
    , m_database(database)
{
    m_refreshDelay.setSingleShot(true);
    m_refreshDelay.setInterval(200);
    connect(&m_refreshDelay, &QTimer::timeout, this, &SiteListModel::refresh);
    connect(&runner, &JobRunner::jobsChanged, this, &SiteListModel::refreshSoon);
    connect(&runner, &JobRunner::jobProgress, this,
            [this](qint64, qint64 siteId, int current, int total, const QString &label) {
                m_progress.insert(siteId,
                                  {total > 0 ? static_cast<double>(current) / total : 0.0, label});
                const int row = rowOf(siteId);
                if (row >= 0) {
                    m_rows[static_cast<size_t>(row)].activeJobStatus = QStringLiteral("running");
                    const QModelIndex changed = index(row);
                    emit dataChanged(changed, changed,
                                     {JobStatusRole, ProgressRole, ProgressLabelRole});
                }
            });
    connect(&runner, &JobRunner::jobFinished, this, [this](qint64, qint64 siteId, const QString &) {
        m_progress.remove(siteId);
        refreshSoon();
    });
}

int SiteListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant SiteListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const SiteStore::SiteRow &row = m_rows.at(static_cast<size_t>(index.row()));
    switch (role) {
    case SiteIdRole:
        return row.id;
    case SiteKeyRole:
        return row.siteKey;
    case UrlRole:
        return row.url;
    case TitleRole:
        return row.title;
    case CmsRole:
        return row.cms;
    case HostingRole:
        return row.hosting;
    case EmailProviderRole:
        return row.emailProvider;
    case HttpStatusRole:
        return row.httpStatus;
    case OutcomeRole:
        return row.outcome;
    case LastFetchedAtRole:
        return row.lastFetchedAt;
    case TlsValidUntilRole:
        return row.tlsValidUntil;
    case DomainExpiresOnRole:
        return row.domainExpiresOn;
    case FaviconUrlRole:
        return row.faviconUrl;
    case SourceRole:
        return row.source;
    case JobStatusRole:
        return row.activeJobStatus;
    case ProgressRole:
        return m_progress.value(row.id).fraction;
    case ProgressLabelRole:
        return m_progress.value(row.id).label;
    default:
        return {};
    }
}

QHash<int, QByteArray> SiteListModel::roleNames() const
{
    return {{SiteIdRole, "siteId"},
            {SiteKeyRole, "siteKey"},
            {UrlRole, "url"},
            {TitleRole, "title"},
            {CmsRole, "cms"},
            {HostingRole, "hosting"},
            {EmailProviderRole, "emailProvider"},
            {HttpStatusRole, "httpStatus"},
            {OutcomeRole, "outcome"},
            {LastFetchedAtRole, "lastFetchedAt"},
            {TlsValidUntilRole, "tlsValidUntil"},
            {DomainExpiresOnRole, "domainExpiresOn"},
            {SourceRole, "source"},
            {FaviconUrlRole, "faviconUrl"},
            {JobStatusRole, "jobStatus"},
            {ProgressRole, "progress"},
            {ProgressLabelRole, "progressLabel"}};
}

void SiteListModel::setSearch(const QString &search)
{
    if (m_search == search) {
        return;
    }
    m_search = search;
    emit searchChanged();
    refreshSoon();
}

void SiteListModel::setFilter(int filter)
{
    const auto value = static_cast<SiteStore::Filter>(
        std::clamp(filter, 0, static_cast<int>(SiteStore::Filter::Expiring)));
    if (m_filter == value) {
        return;
    }
    m_filter = value;
    emit filterChanged();
    refresh();
}

void SiteListModel::refresh()
{
    m_refreshDelay.stop();
    const int generation = ++m_generation;
    SiteStore::ListQuery query;
    query.search = m_search;
    query.filter = m_filter;
    m_database.run(
        this, [query](QSqlDatabase &db) { return SiteStore::listSites(db, query); },
        [this, generation](SiteStore::ListResult result) {
            if (generation == m_generation) {
                apply(std::move(result));
            }
        });
}

void SiteListModel::refreshSoon()
{
    m_refreshDelay.start();
}

void SiteListModel::apply(SiteStore::ListResult result)
{
    if (!result.error.isEmpty()) {
        return;
    }
    std::vector<SiteStore::SiteRow> &next = result.rows;
    const bool sameOrder = next.size() == m_rows.size() &&
                           std::equal(next.begin(), next.end(), m_rows.begin(),
                                      [](const auto &a, const auto &b) { return a.id == b.id; });
    if (sameOrder) {
        for (size_t i = 0; i < next.size(); ++i) {
            if (!(next[i] == m_rows[i])) {
                m_rows[i] = next[i];
                const QModelIndex changed = index(static_cast<int>(i));
                emit dataChanged(changed, changed);
            }
        }
    } else {
        beginResetModel();
        m_rows = std::move(next);
        endResetModel();
    }
    m_total = result.total;
    m_loaded = true;
    emit countChanged();
}

int SiteListModel::rowOf(qint64 siteId) const
{
    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].id == siteId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}
