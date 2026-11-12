#pragma once

#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class Database;
class JobRunner;

// Numbers and short lists for the Dashboard page. Refreshes when jobs finish and when the
// page asks.
class Dashboard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Dashboard is created by the application.")

    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)

public:
    Dashboard(Database &database, JobRunner &runner, QObject *parent = nullptr);

    [[nodiscard]] QVariantMap stats() const { return m_stats; }
    Q_INVOKABLE void refresh();

signals:
    void statsChanged();

private:
    Database &m_database;
    QVariantMap m_stats;
};
