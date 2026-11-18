#include "DueNotifier.h"

#include <QDate>
#include <QSet>
#include <QSqlQuery>

#include "AppSettings.h"
#include "Database.h"
#include "SqlExec.h"

namespace {

constexpr int kCheckEveryMs = 5 * 60 * 1000;

QVariantList loadDue(QSqlDatabase &db, const QString &today)
{
    QVariantList items;
    QSqlQuery todos = SqlExec::run(
        db,
        QStringLiteral("SELECT td.id, td.title, td.due_on, s.id, s.site_key FROM todos td "
                       "JOIN sites s ON s.id = td.site_id WHERE td.done_at IS NULL AND td.due_on IS NOT NULL "
                       "AND td.due_on <= ? AND s.archived_at IS NULL ORDER BY td.due_on, td.id LIMIT 200"),
        {today});
    while (todos.next()) {
        items.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("todo")},
                                 {QStringLiteral("id"), todos.value(0)},
                                 {QStringLiteral("title"), todos.value(1)},
                                 {QStringLiteral("dueOn"), todos.value(2).toString()},
                                 {QStringLiteral("siteId"), todos.value(3)},
                                 {QStringLiteral("siteKey"), todos.value(4)}});
    }
    // A finished or dropped project has no deadline to remind about.
    QSqlQuery deadlines = SqlExec::run(
        db,
        QStringLiteral("SELECT s.id, s.site_key, s.deadline_on FROM sites s WHERE s.deadline_on IS NOT NULL "
                       "AND s.deadline_on <= ? AND s.archived_at IS NULL AND s.status NOT IN ('completed', 'canceled') "
                       "ORDER BY s.deadline_on, s.id LIMIT 200"),
        {today});
    while (deadlines.next()) {
        items.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("deadline")},
                                 {QStringLiteral("id"), deadlines.value(0)},
                                 {QStringLiteral("title"), QString()},
                                 {QStringLiteral("dueOn"), deadlines.value(2).toString()},
                                 {QStringLiteral("siteId"), deadlines.value(0)},
                                 {QStringLiteral("siteKey"), deadlines.value(1)}});
    }
    return items;
}

} // namespace

DueNotifier::DueNotifier(Database &database, AppSettings &settings, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_settings(settings)
{
    m_timer.setInterval(kCheckEveryMs);
    connect(&m_timer, &QTimer::timeout, this, &DueNotifier::check);
}

void DueNotifier::start()
{
    m_timer.start();
    check();
}

void DueNotifier::check()
{
    if (!m_settings.notifyDue()) {
        return;
    }
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    m_database.run(
        this, [today](QSqlDatabase &db) { return loadDue(db, today); },
        [this, today](const QVariantList &due) {
            // "kind:id:stage:day". Today's keys are all that matter: an overdue item is announced
            // again tomorrow, which is the reminder.
            const QString suffix = QLatin1Char(':') + today;
            QSet<QString> shown;
            for (const QString &key : m_settings.dueNoticesShown()) {
                if (key.endsWith(suffix)) {
                    shown.insert(key);
                }
            }
            QVariantList fresh;
            for (const QVariant &entry : due) {
                QVariantMap item = entry.toMap();
                const bool overdue = item.value(QStringLiteral("dueOn")).toString() < today;
                const QString key = item.value(QStringLiteral("kind")).toString() + QLatin1Char(':')
                                    + item.value(QStringLiteral("id")).toString() + QLatin1Char(':')
                                    + (overdue ? QStringLiteral("overdue") : QStringLiteral("today")) + suffix;
                if (shown.contains(key)) {
                    continue;
                }
                shown.insert(key);
                item.insert(QStringLiteral("overdue"), overdue);
                fresh.append(item);
            }
            m_settings.setDueNoticesShown(QStringList(shown.begin(), shown.end()));
            if (!fresh.isEmpty()) {
                emit dueItems(fresh);
            }
        });
}
