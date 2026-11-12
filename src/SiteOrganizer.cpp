#include "SiteOrganizer.h"

#include "DbTime.h"
#include "SiteStore.h"
#include "SqlExec.h"

namespace SiteOrganizer {

namespace {

constexpr int kMaxNameLength = 120;

// The id of the row in a table with a unique "name" column, created when missing.
qint64 idForName(QSqlDatabase &db, const QString &table, const QString &name)
{
    QSqlQuery existing = SqlExec::run(
        db, QStringLiteral("SELECT id FROM %1 WHERE name = ?").arg(table), {name});
    if (existing.next()) {
        return existing.value(0).toLongLong();
    }
    QString error;
    QSqlQuery insert = SqlExec::run(
        db, QStringLiteral("INSERT INTO %1 (name, created_at) VALUES (?, ?)").arg(table),
        {name, DbTime::now()}, &error);
    return error.isEmpty() ? insert.lastInsertId().toLongLong() : 0;
}

bool succeeded(QSqlDatabase &db, const QString &sql, std::initializer_list<QVariant> binds)
{
    QString error;
    SqlExec::run(db, sql, binds, &error);
    return error.isEmpty();
}

QStringList names(QSqlDatabase &db, const QString &sql)
{
    QStringList result;
    QSqlQuery query = SqlExec::run(db, sql);
    while (query.next()) {
        result.append(query.value(0).toString());
    }
    return result;
}

} // namespace

QStringList statuses()
{
    return {QStringLiteral("none"),      QStringLiteral("consider"),  QStringLiteral("design"),
            QStringLiteral("contacted"), QStringLiteral("replied"),   QStringLiteral("client"),
            QStringLiteral("completed"), QStringLiteral("canceled")};
}

bool setStatus(QSqlDatabase &db, qint64 siteId, const QString &status, const QString &activityText)
{
    if (!statuses().contains(status)) {
        return false;
    }
    const bool saved = succeeded(
        db, QStringLiteral("UPDATE sites SET status = ?, updated_at = ? WHERE id = ?"),
        {status, DbTime::now(), siteId});
    if (saved) {
        SiteStore::logActivity(db, siteId, QStringLiteral("site.status"),
                               activityText.arg(status));
    }
    return saved;
}

bool setDeadline(QSqlDatabase &db, qint64 siteId, const QString &dueOn)
{
    return succeeded(db, QStringLiteral("UPDATE sites SET deadline_on = ?, updated_at = ? WHERE id = ?"),
                     {SqlExec::textOrNull(dueOn), DbTime::now(), siteId});
}

bool applyMailStatus(QSqlDatabase &db, qint64 siteId)
{
    QSqlQuery current = SqlExec::run(db, QStringLiteral("SELECT status FROM sites WHERE id = ?"), {siteId});
    if (!current.next()) {
        return false;
    }
    // Real replies only: bounces and auto-replies do not count (PRD).
    QSqlQuery mail = SqlExec::run(
        db,
        QStringLiteral(
            "SELECT COALESCE(MAX(CASE WHEN direction = 'sent' THEN 1 ELSE 0 END), 0), "
            "COALESCE(MAX(CASE WHEN direction = 'received' AND is_bounce = 0 AND is_auto_reply = 0 "
            "THEN 1 ELSE 0 END), 0) FROM mail_messages WHERE site_id = ?"),
        {siteId});
    if (!mail.next()) {
        return false;
    }
    const QString target = mail.value(1).toInt() != 0   ? QStringLiteral("replied")
                           : mail.value(0).toInt() != 0 ? QStringLiteral("contacted")
                                                         : QString();
    const QStringList order = statuses();
    // Mail only moves a site forward; client, completed and canceled are never touched.
    if (target.isEmpty() || order.indexOf(current.value(0).toString()) >= order.indexOf(target)) {
        return false;
    }
    return setStatus(db, siteId, target, QObject::tr("Status set to %1 (from email)"));
}

bool setCategory(QSqlDatabase &db, qint64 siteId, const QString &name)
{
    const QString trimmed = name.trimmed().left(kMaxNameLength);
    QVariant categoryId = SqlExec::nullInt();
    if (!trimmed.isEmpty()) {
        const qint64 id = idForName(db, QStringLiteral("categories"), trimmed);
        if (id == 0) {
            return false;
        }
        categoryId = id;
    }
    return succeeded(db, QStringLiteral("UPDATE sites SET category_id = ?, updated_at = ? WHERE id = ?"),
                     {categoryId, DbTime::now(), siteId});
}

bool addTag(QSqlDatabase &db, qint64 siteId, const QString &name)
{
    const QString trimmed = name.trimmed().left(kMaxNameLength);
    if (trimmed.isEmpty()) {
        return false;
    }
    const qint64 tagId = idForName(db, QStringLiteral("tags"), trimmed);
    if (tagId == 0) {
        return false;
    }
    return succeeded(db,
                     SqlExec::insertIgnore(db) +
                         QStringLiteral(" INTO site_tags (site_id, tag_id) VALUES (?, ?)"),
                     {siteId, tagId});
}

bool removeTag(QSqlDatabase &db, qint64 siteId, qint64 tagId)
{
    return succeeded(db, QStringLiteral("DELETE FROM site_tags WHERE site_id = ? AND tag_id = ?"),
                     {siteId, tagId});
}

bool addNote(QSqlDatabase &db, qint64 siteId, const QString &title, const QString &body)
{
    const QString trimmed = body.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    const QString now = DbTime::now();
    return succeeded(db,
                     QStringLiteral("INSERT INTO notes (site_id, title, body, created_at, updated_at) "
                                    "VALUES (?, ?, ?, ?, ?)"),
                     {siteId, title.trimmed().left(200) + QStringLiteral(""), trimmed, now, now}); // never NULL
}

bool deleteNote(QSqlDatabase &db, qint64 noteId)
{
    return succeeded(db, QStringLiteral("DELETE FROM notes WHERE id = ?"), {noteId});
}

bool addTodo(QSqlDatabase &db, qint64 siteId, const QString &title, const QString &dueOn)
{
    const QString trimmed = title.trimmed().left(500);
    if (trimmed.isEmpty()) {
        return false;
    }
    return succeeded(db,
                     QStringLiteral("INSERT INTO todos (site_id, title, due_on, created_at) "
                                    "VALUES (?, ?, ?, ?)"),
                     {siteId, trimmed, SqlExec::textOrNull(dueOn), DbTime::now()});
}

bool setTodoDone(QSqlDatabase &db, qint64 todoId, bool done)
{
    const QVariant doneAt =
        done ? QVariant(DbTime::now()) : QVariant(QMetaType(QMetaType::QString));
    return succeeded(db, QStringLiteral("UPDATE todos SET done_at = ? WHERE id = ?"),
                     {doneAt, todoId});
}

bool deleteTodo(QSqlDatabase &db, qint64 todoId)
{
    return succeeded(db, QStringLiteral("DELETE FROM todos WHERE id = ?"), {todoId});
}

QVariantMap load(QSqlDatabase &db, qint64 siteId)
{
    QVariantMap result;

    QSqlQuery site = SqlExec::run(
        db,
        QStringLiteral("SELECT s.status, c.name, s.deadline_on FROM sites s LEFT JOIN categories c ON "
                       "c.id = s.category_id WHERE s.id = ?"),
        {siteId});
    if (site.next()) {
        result.insert(QStringLiteral("status"), site.value(0).toString());
        result.insert(QStringLiteral("category"), site.value(1).toString());
        result.insert(QStringLiteral("deadlineOn"), site.value(2).toString());
    }

    QVariantList tags;
    QSqlQuery tagQuery = SqlExec::run(
        db,
        QStringLiteral("SELECT t.id, t.name FROM site_tags st JOIN tags t ON t.id = st.tag_id "
                       "WHERE st.site_id = ? ORDER BY t.name"),
        {siteId});
    while (tagQuery.next()) {
        tags.append(QVariantMap{{QStringLiteral("id"), tagQuery.value(0)},
                                {QStringLiteral("name"), tagQuery.value(1)}});
    }
    result.insert(QStringLiteral("tags"), tags);

    QVariantList notes;
    QSqlQuery noteQuery = SqlExec::run(
        db,
        QStringLiteral("SELECT id, title, body, created_at FROM notes WHERE site_id = ? ORDER BY id DESC"),
        {siteId});
    while (noteQuery.next()) {
        notes.append(QVariantMap{{QStringLiteral("id"), noteQuery.value(0)},
                                 {QStringLiteral("title"), noteQuery.value(1)},
                                 {QStringLiteral("body"), noteQuery.value(2)},
                                 {QStringLiteral("createdAt"), DbTime::toIso(noteQuery.value(3))}});
    }
    result.insert(QStringLiteral("notes"), notes);

    // Open to-dos first, soonest due date first; finished ones sink to the end.
    QVariantList todos;
    QSqlQuery todoQuery = SqlExec::run(
        db,
        QStringLiteral("SELECT id, title, due_on, done_at FROM todos WHERE site_id = ? ORDER BY "
                       "(CASE WHEN done_at IS NULL THEN 0 ELSE 1 END), "
                       "(CASE WHEN due_on IS NULL THEN 1 ELSE 0 END), due_on, id"),
        {siteId});
    while (todoQuery.next()) {
        todos.append(QVariantMap{{QStringLiteral("id"), todoQuery.value(0)},
                                 {QStringLiteral("title"), todoQuery.value(1)},
                                 {QStringLiteral("dueOn"), todoQuery.value(2).toString()},
                                 {QStringLiteral("done"), !todoQuery.value(3).isNull()}});
    }
    result.insert(QStringLiteral("todos"), todos);

    result.insert(QStringLiteral("categories"),
                  names(db, QStringLiteral("SELECT name FROM categories ORDER BY name")));
    result.insert(QStringLiteral("allTags"),
                  names(db, QStringLiteral("SELECT name FROM tags ORDER BY name")));
    return result;
}

} // namespace SiteOrganizer
