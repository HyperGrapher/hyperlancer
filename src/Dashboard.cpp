#include "Dashboard.h"

#include "Database.h"
#include "JobRunner.h"
#include "SiteStore.h"

Dashboard::Dashboard(Database &database, JobRunner &runner, QObject *parent)
    : QObject(parent)
    , m_database(database)
{
    connect(&runner, &JobRunner::jobFinished, this, &Dashboard::refresh);
}

void Dashboard::refresh()
{
    m_database.run(
        this, [](QSqlDatabase &db) { return SiteStore::dashboard(db); },
        [this](const QVariantMap &stats) {
            m_stats = stats;
            emit statsChanged();
        });
}
