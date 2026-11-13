#include "NoteList.h"

#include <QSqlQuery>

#include "Database.h"
#include "SqlExec.h"

namespace {

QVariantList load(QSqlDatabase &db)
{
    QVariantList items;
    QSqlQuery query = SqlExec::run(
        db, QStringLiteral("SELECT n.id, n.title, n.body, n.created_at, s.id, s.site_key FROM notes n "
                           "JOIN sites s ON s.id = n.site_id WHERE s.archived_at IS NULL "
                           "ORDER BY s.site_key, n.id DESC LIMIT 2000"));
    while (query.next()) {
        items.append(QVariantMap{{QStringLiteral("id"), query.value(0)},
                                 {QStringLiteral("title"), query.value(1)},
                                 {QStringLiteral("body"), query.value(2)},
                                 {QStringLiteral("createdAt"), query.value(3).toString()},
                                 {QStringLiteral("siteId"), query.value(4)},
                                 {QStringLiteral("siteKey"), query.value(5)}});
    }
    return items;
}

} // namespace

NoteList::NoteList(Database &database, QObject *parent)
    : QObject(parent)
    , m_database(database)
{}

void NoteList::refresh()
{
    const int generation = ++m_generation;
    m_database.run(this, [](QSqlDatabase &db) { return load(db); },
                   [this, generation](const QVariantList &items) {
                       if (generation != m_generation) {
                           return;
                       }
                       m_items = items;
                       emit itemsChanged();
                   });
}
