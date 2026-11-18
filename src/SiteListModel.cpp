#include "SiteListModel.h"

#include <algorithm>

#include "Database.h"
#include "JobRunner.h"
#include "SiteOrganizer.h"
#include "SqlExec.h"

SiteListModel::SiteListModel(Database &database, JobRunner &runner, QObject *parent)
    : QAbstractListModel(parent)
    , m_database(database)
    , m_runner(runner)
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
    case SelectedRole:
        return m_selected.contains(row.id);
    case DeadlineOnRole:
        return row.deadlineOn;
    case StatusRole:
        return row.status;
    case CategoryRole:
        return row.category;
    case TagsRole:
        return row.tags;
    case OpenTodosRole:
        return row.openTodos;
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
            {StatusRole, "status"},
            {SelectedRole, "selected"},
            {DeadlineOnRole, "deadlineOn"},
            {CategoryRole, "category"},
            {TagsRole, "tags"},
            {OpenTodosRole, "openTodos"},
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
        std::clamp(filter, 0, static_cast<int>(SiteStore::Filter::Archived)));
    if (m_filter == value) {
        return;
    }
    m_filter = value;
    emit filterChanged();
    refresh();
}

void SiteListModel::setStatusFilter(const QString &status)
{
    if (m_statusFilter == status) {
        return;
    }
    m_statusFilter = status;
    emit statusFilterChanged();
    refresh();
}

void SiteListModel::setCategoryFilter(const QString &category)
{
    if (m_categoryFilter == category) {
        return;
    }
    m_categoryFilter = category;
    emit categoryFilterChanged();
    refresh();
}

QVariantList SiteListModel::selectedIds() const
{
    QVariantList ids;
    for (const qint64 id : m_selected) {
        ids.append(id);
    }
    return ids;
}

QVariantMap SiteListModel::currentConfig() const
{
    return {{QStringLiteral("search"), m_search},
            {QStringLiteral("filter"), static_cast<int>(m_filter)},
            {QStringLiteral("status"), m_statusFilter},
            {QStringLiteral("category"), m_categoryFilter},
            {QStringLiteral("sortKey"), static_cast<int>(m_sortKey)},
            {QStringLiteral("sortDescending"), m_sortDescending}};
}

void SiteListModel::loadSavedViews()
{
    m_database.run(
        this, [](QSqlDatabase &db) { return SiteStore::listSavedViews(db); },
        [this](const QVariantList &views) {
            m_savedViews = views;
            emit savedViewsChanged();
        });
}

void SiteListModel::saveView(const QString &name)
{
    const QVariantMap config = currentConfig();
    m_database.run(
        this, [name, config](QSqlDatabase &db) { return SiteStore::saveView(db, name, config); },
        [this, config](qint64 id) {
            if (id == 0) {
                return;
            }
            m_activeViewId = id;
            m_activeViewConfig = config;
            emit activeViewChanged();
            loadSavedViews();
        });
}

void SiteListModel::applyView(qint64 viewId)
{
    if (viewId == 0) {
        // "All websites": the default list.
        m_activeViewId = 0;
        setSearch(QString());
        setFilter(0);
        setStatusFilter(QString());
        setCategoryFilter(QString());
        setSortKey(0);
        setSortDescending(true);
        emit activeViewChanged();
        return;
    }
    for (const QVariant &entry : std::as_const(m_savedViews)) {
        const QVariantMap view = entry.toMap();
        if (view.value(QStringLiteral("id")).toLongLong() != viewId) {
            continue;
        }
        const QVariantMap config = view.value(QStringLiteral("config")).toMap();
        setSearch(config.value(QStringLiteral("search")).toString());
        setFilter(config.value(QStringLiteral("filter")).toInt());
        setStatusFilter(config.value(QStringLiteral("status")).toString());
        setCategoryFilter(config.value(QStringLiteral("category")).toString());
        setSortKey(config.value(QStringLiteral("sortKey")).toInt());
        setSortDescending(config.value(QStringLiteral("sortDescending"), true).toBool());
        m_activeViewId = viewId;
        m_activeViewConfig = currentConfig(); // what the setters actually produced
        emit activeViewChanged();
        return;
    }
}

void SiteListModel::deleteView(qint64 viewId)
{
    m_database.run(
        this, [viewId](QSqlDatabase &db) { return SiteStore::deleteSavedView(db, viewId); },
        [this, viewId](bool) {
            if (m_activeViewId == viewId) {
                m_activeViewId = 0;
                emit activeViewChanged();
            }
            loadSavedViews();
        });
}

void SiteListModel::toggleSelected(qint64 siteId)
{
    const int row = rowOf(siteId);
    if (row < 0) {
        return;
    }
    if (!m_selected.remove(siteId)) {
        m_selected.insert(siteId);
    }
    const QModelIndex changed = index(row);
    emit dataChanged(changed, changed, {SelectedRole});
    emit selectionChanged();
}

void SiteListModel::selectAll()
{
    for (const SiteStore::SiteRow &row : m_rows) {
        m_selected.insert(row.id);
    }
    if (!m_rows.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1), {SelectedRole});
    }
    emit selectionChanged();
}

void SiteListModel::clearSelection()
{
    if (m_selected.isEmpty()) {
        return;
    }
    m_selected.clear();
    if (!m_rows.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1), {SelectedRole});
    }
    emit selectionChanged();
}

// Runs the change for every selected website in one transaction, then refreshes the list.
void SiteListModel::runOnSelected(std::function<bool(QSqlDatabase &, qint64)> operation)
{
    const QList<qint64> ids = m_selected.values();
    m_database.run(
        this,
        [ids, operation = std::move(operation)](QSqlDatabase &db) {
            SqlExec::Transaction transaction(db);
            for (const qint64 id : ids) {
                operation(db, id);
            }
            return transaction.commit();
        },
        [this](bool) { refresh(); });
}

void SiteListModel::setStatusForSelected(const QString &status)
{
    runOnSelected([status](QSqlDatabase &db, qint64 id) { return SiteOrganizer::setStatus(db, id, status); });
}

void SiteListModel::setCategoryForSelected(const QString &name)
{
    runOnSelected([name](QSqlDatabase &db, qint64 id) { return SiteOrganizer::setCategory(db, id, name); });
}

void SiteListModel::addTagToSelected(const QString &name)
{
    runOnSelected([name](QSqlDatabase &db, qint64 id) { return SiteOrganizer::addTag(db, id, name); });
}

void SiteListModel::archiveSelected(bool archived)
{
    runOnSelected([archived](QSqlDatabase &db, qint64 id) { return SiteStore::setArchived(db, id, archived); });
    emit archiveChanged();
}

void SiteListModel::analyzeSelected()
{
    for (const SiteStore::SiteRow &row : m_rows) {
        if (m_selected.contains(row.id) && row.activeJobStatus.isEmpty()) {
            m_runner.enqueueProfile(row.id, row.url);
        }
    }
}

void SiteListModel::setSiteStatus(qint64 siteId, const QString &status)
{
    m_database.run(
        this, [siteId, status](QSqlDatabase &db) { return SiteOrganizer::setStatus(db, siteId, status); },
        [this](bool) { refresh(); });
}

void SiteListModel::setSortKey(int key)
{
    const auto value = static_cast<SiteStore::SortKey>(
        std::clamp(key, 0, static_cast<int>(SiteStore::SortKey::Todos)));
    if (m_sortKey == value) {
        return;
    }
    m_sortKey = value;
    emit sortChanged();
    refresh();
}

void SiteListModel::setSortDescending(bool descending)
{
    if (m_sortDescending == descending) {
        return;
    }
    m_sortDescending = descending;
    emit sortChanged();
    refresh();
}

void SiteListModel::refresh()
{
    if (m_activeViewId != 0 && currentConfig() != m_activeViewConfig) {
        m_activeViewId = 0;
        emit activeViewChanged();
    }
    m_refreshDelay.stop();
    const int generation = ++m_generation;
    SiteStore::ListQuery query;
    query.search = m_search;
    query.filter = m_filter;
    query.status = m_statusFilter;
    query.category = m_categoryFilter;
    query.sortKey = m_sortKey;
    query.descending = m_sortDescending;
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
    const qsizetype selectedBefore = m_selected.size();
    for (auto it = m_selected.begin(); it != m_selected.end();) {
        it = rowOf(*it) < 0 ? m_selected.erase(it) : std::next(it);
    }
    if (m_selected.size() != selectedBefore) {
        emit selectionChanged();
    }
    m_total = result.total;
    m_categories = result.categories;
    m_tags = result.tags;
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
