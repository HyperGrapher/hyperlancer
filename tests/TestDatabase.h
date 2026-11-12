#pragma once

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>
#include <QUuid>

#include "Migrations.h"

// A migrated SQLite database in a temporary folder, opened on the test thread.
class TestDatabase
{
public:
    TestDatabase()
        : m_name(QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        db.setDatabaseName(m_dir.filePath(QStringLiteral("test.sqlite")));
        m_opened = db.open();
        QSqlQuery(db).exec(QStringLiteral("PRAGMA foreign_keys = ON"));
        m_result = Migrations::apply(db, QStringLiteral(":/migrations/sqlite"));
    }
    ~TestDatabase()
    {
        {
            QSqlDatabase db = QSqlDatabase::database(m_name, false);
            db.close();
        }
        QSqlDatabase::removeDatabase(m_name);
    }
    TestDatabase(const TestDatabase &) = delete;
    TestDatabase &operator=(const TestDatabase &) = delete;

    [[nodiscard]] bool isReady() const { return m_opened && m_result.ok; }
    [[nodiscard]] const Migrations::Result &migration() const { return m_result; }
    [[nodiscard]] QSqlDatabase db() const { return QSqlDatabase::database(m_name, false); }
    [[nodiscard]] QString dir() const { return m_dir.path(); }

private:
    QTemporaryDir m_dir;
    QString m_name;
    bool m_opened = false;
    Migrations::Result m_result;
};
