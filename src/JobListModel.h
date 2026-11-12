#pragma once

#include <vector>

#include <QAbstractListModel>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include "JobStore.h"

class Database;
class JobRunner;

// Recent jobs for the Jobs page, newest first with unfinished ones on top. Progress of
// running jobs is applied in place from JobRunner signals; the rest refreshes from the
// database, at most a few times per second.
class JobListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("JobListModel is created by the application.")

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countChanged)

public:
    enum Role
    {
        JobIdRole = Qt::UserRole + 1,
        TypeRole,
        SiteIdRole,
        SiteKeyRole,
        StatusRole,
        ProgressRole,
        ProgressLabelRole,
        ErrorRole,
        CreatedAtRole,
        StartedAtRole,
        FinishedAtRole,
        IsActiveRole
    };

    JobListModel(Database &database, JobRunner &runner, QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int activeCount() const;

    Q_INVOKABLE void refresh();

signals:
    void countChanged();

private:
    void apply(std::vector<JobStore::JobRow> rows);
    void applyProgress(qint64 jobId, int current, int total, const QString &label);

    Database &m_database;
    std::vector<JobStore::JobRow> m_rows;
    QTimer m_refreshDelay;
};
