#pragma once

#include <QString>

// Which database holds the records. SQLite needs nothing but a file; MySQL is the remote
// option from the PRD. The password is not part of the stored settings: it lives in the OS
// credential store and is filled in just before connecting.
struct DatabaseConfig
{
    enum class Backend
    {
        Sqlite,
        MySql
    };

    Backend backend = Backend::Sqlite;
    QString sqlitePath;
    QString host;
    int port = 3306;
    QString databaseName;
    QString user;
    QString password;
    bool requireTls = true;

    [[nodiscard]] bool isMySql() const { return backend == Backend::MySql; }
    [[nodiscard]] QString driverName() const;
    // Folder name of the migrations for this backend inside the resources.
    [[nodiscard]] QString migrationsDir() const;
    // Short human label, e.g. "MySQL db.example.com/leads".
    [[nodiscard]] QString describe() const;
    [[nodiscard]] QString validate() const;
};
