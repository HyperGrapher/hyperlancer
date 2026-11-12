#include "JobListModel.h"

#include "Database.h"
#include "JobRunner.h"

namespace {

constexpr int kRowLimit = 200;

bool isActive(const QString &status)
{
    return status == QStringLiteral("queued") || status == QStringLiteral("running");
}

} // namespace

JobListModel::JobListModel(Database &database, JobRunner &runner, QObject *parent)
    : QAbstractListModel(parent)
    , m_database(database)
{
    m_refreshDelay.setSingleShot(true);
    m_refreshDelay.setInterval(250);
    connect(&m_refreshDelay, &QTimer::timeout, this, &JobListModel::refresh);
    connect(&runner, &JobRunner::jobsChanged, &m_refreshDelay, qOverload<>(&QTimer::start));
    const auto onProgress = [this](qint64 jobId, qint64, int current, int total, const QString &label) {
        applyProgress(jobId, current, total, label);
    };
    connect(&runner, &JobRunner::jobProgress, this, onProgress);
    connect(&runner, &JobRunner::crawlProgress, this, onProgress);
}

int JobListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant JobListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const JobStore::JobRow &row = m_rows.at(static_cast<size_t>(index.row()));
    switch (role) {
    case JobIdRole:
        return row.id;
    case TypeRole:
        return row.type;
    case SiteIdRole:
        return row.siteId;
    case SiteKeyRole:
        return row.siteKey;
    case StatusRole:
        return row.status;
    case ProgressRole:
        return row.progressTotal > 0 ? static_cast<double>(row.progressCurrent) / row.progressTotal
                                     : 0.0;
    case ProgressLabelRole:
        return row.progressLabel;
    case ErrorRole:
        return row.error;
    case CreatedAtRole:
        return row.createdAt;
    case StartedAtRole:
        return row.startedAt;
    case FinishedAtRole:
        return row.finishedAt;
    case IsActiveRole:
        return isActive(row.status);
    default:
        return {};
    }
}

QHash<int, QByteArray> JobListModel::roleNames() const
{
    return {{JobIdRole, "jobId"},
            {TypeRole, "type"},
            {SiteIdRole, "siteId"},
            {SiteKeyRole, "siteKey"},
            {StatusRole, "status"},
            {ProgressRole, "progress"},
            {ProgressLabelRole, "progressLabel"},
            {ErrorRole, "error"},
            {CreatedAtRole, "createdAt"},
            {StartedAtRole, "startedAt"},
            {FinishedAtRole, "finishedAt"},
            {IsActiveRole, "isActive"}};
}

int JobListModel::activeCount() const
{
    int count = 0;
    for (const JobStore::JobRow &row : m_rows) {
        if (isActive(row.status)) {
            ++count;
        }
    }
    return count;
}

void JobListModel::refresh()
{
    m_database.run(
        this, [](QSqlDatabase &db) { return JobStore::recent(db, kRowLimit); },
        [this](std::vector<JobStore::JobRow> rows) { apply(std::move(rows)); });
}

void JobListModel::apply(std::vector<JobStore::JobRow> rows)
{
    const bool sameShape = rows.size() == m_rows.size() &&
                           std::equal(rows.begin(), rows.end(), m_rows.begin(),
                                      [](const auto &a, const auto &b) { return a.id == b.id; });
    if (sameShape) {
        m_rows = std::move(rows);
        if (!m_rows.empty()) {
            emit dataChanged(index(0), index(rowCount() - 1));
        }
    } else {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }
    emit countChanged();
}

void JobListModel::applyProgress(qint64 jobId, int current, int total, const QString &label)
{
    for (size_t i = 0; i < m_rows.size(); ++i) {
        JobStore::JobRow &row = m_rows[i];
        if (row.id != jobId) {
            continue;
        }
        row.progressCurrent = current;
        row.progressTotal = total;
        row.progressLabel = label;
        if (row.status == QStringLiteral("queued")) {
            row.status = QStringLiteral("running");
        }
        const QModelIndex changed = index(static_cast<int>(i));
        emit dataChanged(changed, changed, {StatusRole, ProgressRole, ProgressLabelRole});
        return;
    }
}
