#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class Database;

// Every to-do of every website, for the To-dos page. Open ones soonest first (undated last),
// completed ones newest first, or all of them grouped by website.
class TodoList : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("TodoList is created by the application.")

    // { id, title, dueOn, done, siteId, siteKey }
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    // 0 = open, 1 = completed, 2 = all (ordered by website).
    Q_PROPERTY(int filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int openCount READ openCount NOTIFY itemsChanged)

public:
    explicit TodoList(Database &database, QObject *parent = nullptr);

    [[nodiscard]] QVariantList items() const { return m_items; }
    enum Filter { Open, Completed, All };
    Q_ENUM(Filter)

    [[nodiscard]] int filter() const { return m_filter; }
    void setFilter(int filter);
    [[nodiscard]] int openCount() const { return m_openCount; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setDone(qint64 todoId, bool done);
    Q_INVOKABLE void remove(qint64 todoId);

signals:
    void itemsChanged();
    void filterChanged();

private:
    Database &m_database;
    QVariantList m_items;
    int m_filter = Open;
    int m_openCount = 0;
    int m_generation = 0;
};
