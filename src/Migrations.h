#pragma once

#include <vector>

#include <QSqlDatabase>
#include <QString>
#include <QStringList>

// Numbered SQL files ("0001_init.sql") applied in order and recorded in schema_migrations.
// SQLite and MySQL each have their own folder with the same version numbers.
namespace Migrations {

struct Migration
{
    int version = 0;
    QString name;
    QString sql;
};

struct Result
{
    bool ok = false;
    QString error;
    int applied = 0;
    int currentVersion = 0;
};

// Reads every NNNN_name.sql in dir, sorted by version. Files with other names are ignored.
[[nodiscard]] std::vector<Migration> load(const QString &dir);

// Splits a script into statements at semicolons outside quotes and comments. Comments are
// dropped; empty statements are skipped.
[[nodiscard]] QStringList splitStatements(const QString &sql);

// Applies the migrations newer than the recorded version. A failing migration stops the run;
// SQLite rolls it back, MySQL cannot (DDL commits implicitly), so its statements must be
// safe to re-run or be fixed by hand, which the error message says.
[[nodiscard]] Result apply(QSqlDatabase &db, const QString &dir);

} // namespace Migrations
