#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class Database;

// Every to-do of every website, for the To-dos page: open ones soonest first (undated last),
// or the completed ones, newest first.
class TodoList : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("TodoList is created by the application.")

    // { id, title, dueOn, done, siteId, siteKey }
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(bool showDone READ showDone WRITE setShowDone NOTIFY showDoneChanged)
    Q_PROPERTY(int openCount READ openCount NOTIFY itemsChanged)

public:
    explicit TodoList(Database &database, QObject *parent = nullptr);

    [[nodiscard]] QVariantList items() const { return m_items; }
    [[nodiscard]] bool showDone() const { return m_showDone; }
    void setShowDone(bool showDone);
    [[nodiscard]] int openCount() const { return m_openCount; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setDone(qint64 todoId, bool done);
    Q_INVOKABLE void remove(qint64 todoId);

signals:
    void itemsChanged();
    void showDoneChanged();

private:
    Database &m_database;
    QVariantList m_items;
    bool m_showDone = false;
    int m_openCount = 0;
    int m_generation = 0;
};
