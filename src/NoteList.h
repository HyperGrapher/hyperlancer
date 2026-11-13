#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class Database;

// Every note of every website, for the Notes page: ordered by website, newest note first.
// Read-only here; notes are edited on the website's own page.
class NoteList : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("NoteList is created by the application.")

    // { id, title, body, createdAt, siteId, siteKey }
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)

public:
    explicit NoteList(Database &database, QObject *parent = nullptr);

    [[nodiscard]] QVariantList items() const { return m_items; }

    Q_INVOKABLE void refresh();

signals:
    void itemsChanged();

private:
    Database &m_database;
    QVariantList m_items;
    int m_generation = 0;
};
