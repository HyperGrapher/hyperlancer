#include "TodoList.h"

#include <QSqlQuery>

#include "Database.h"
#include "SiteOrganizer.h"
#include "SqlExec.h"

namespace {

struct Loaded
{
    QVariantList items;
    int openCount = 0;
};

Loaded load(QSqlDatabase &db, bool showDone)
{
    Loaded loaded;
    const QString where = showDone ? QStringLiteral("td.done_at IS NOT NULL") : QStringLiteral("td.done_at IS NULL");
    // Open to-dos: dated ones first, soonest first. Completed: newest first.
    const QString order = showDone ? QStringLiteral("td.done_at DESC, td.id DESC")
                                   : QStringLiteral("(CASE WHEN td.due_on IS NULL THEN 1 ELSE 0 END), td.due_on, td.id");
    QSqlQuery query = SqlExec::run(
        db,
        QStringLiteral("SELECT td.id, td.title, td.due_on, td.done_at, s.id, s.site_key FROM todos td "
                       "JOIN sites s ON s.id = td.site_id WHERE s.archived_at IS NULL AND %1 ORDER BY %2 LIMIT 500")
            .arg(where, order));
    while (query.next()) {
        loaded.items.append(QVariantMap{{QStringLiteral("id"), query.value(0)},
                                        {QStringLiteral("title"), query.value(1)},
                                        {QStringLiteral("dueOn"), query.value(2).toString()},
                                        {QStringLiteral("done"), !query.value(3).isNull()},
                                        {QStringLiteral("siteId"), query.value(4)},
                                        {QStringLiteral("siteKey"), query.value(5)}});
    }
    QSqlQuery open = SqlExec::run(
        db, QStringLiteral("SELECT COUNT(*) FROM todos td JOIN sites s ON s.id = td.site_id WHERE "
                           "s.archived_at IS NULL AND td.done_at IS NULL"));
    if (open.next()) {
        loaded.openCount = open.value(0).toInt();
    }
    return loaded;
}

} // namespace

TodoList::TodoList(Database &database, QObject *parent)
    : QObject(parent)
    , m_database(database)
{}

void TodoList::setShowDone(bool showDone)
{
    if (m_showDone == showDone) {
        return;
    }
    m_showDone = showDone;
    emit showDoneChanged();
    refresh();
}

void TodoList::refresh()
{
    const int generation = ++m_generation;
    const bool showDone = m_showDone;
    m_database.run(
        this, [showDone](QSqlDatabase &db) { return load(db, showDone); },
        [this, generation](const Loaded &loaded) {
            if (generation != m_generation) {
                return;
            }
            m_items = loaded.items;
            m_openCount = loaded.openCount;
            emit itemsChanged();
        });
}

void TodoList::setDone(qint64 todoId, bool done)
{
    m_database.run(
        this, [todoId, done](QSqlDatabase &db) { return SiteOrganizer::setTodoDone(db, todoId, done); },
        [this](bool) { refresh(); });
}

void TodoList::remove(qint64 todoId)
{
    m_database.run(
        this, [todoId](QSqlDatabase &db) { return SiteOrganizer::deleteTodo(db, todoId); },
        [this](bool) { refresh(); });
}
