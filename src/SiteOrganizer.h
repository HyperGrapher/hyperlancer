#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// What the user records about a site: lead status, category, tags, notes and to-dos. Plain
// functions over a connection, run on the database thread like SiteStore.
namespace SiteOrganizer {

// Lead statuses in pipeline order. The first one is the default for new sites.
[[nodiscard]] QStringList statuses();

bool setStatus(QSqlDatabase &db, qint64 siteId, const QString &status);
// Empty name removes the category. Unknown names are created.
bool setCategory(QSqlDatabase &db, qint64 siteId, const QString &name);
bool addTag(QSqlDatabase &db, qint64 siteId, const QString &name);
bool removeTag(QSqlDatabase &db, qint64 siteId, qint64 tagId);

bool addNote(QSqlDatabase &db, qint64 siteId, const QString &body);
bool deleteNote(QSqlDatabase &db, qint64 noteId);

bool addTodo(QSqlDatabase &db, qint64 siteId, const QString &title, const QString &dueOn);
bool setTodoDone(QSqlDatabase &db, qint64 todoId, bool done);
bool deleteTodo(QSqlDatabase &db, qint64 todoId);

// "tags" (id, name), "notes" (id, body, createdAt), "todos" (id, title, dueOn, done), plus
// "categories" and "allTags": every name in use, for the pickers.
[[nodiscard]] QVariantMap load(QSqlDatabase &db, qint64 siteId);

} // namespace SiteOrganizer
