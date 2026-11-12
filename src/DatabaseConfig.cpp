#include "DatabaseConfig.h"

#include <QObject>

QString DatabaseConfig::driverName() const
{
    return isMySql() ? QStringLiteral("QMYSQL") : QStringLiteral("QSQLITE");
}

QString DatabaseConfig::migrationsDir() const
{
    return isMySql() ? QStringLiteral(":/migrations/mysql") : QStringLiteral(":/migrations/sqlite");
}

QString DatabaseConfig::describe() const
{
    if (!isMySql()) {
        return QObject::tr("SQLite %1").arg(sqlitePath);
    }
    return QObject::tr("MySQL %1:%2/%3").arg(host).arg(port).arg(databaseName);
}

QString DatabaseConfig::validate() const
{
    if (!isMySql()) {
        return sqlitePath.isEmpty() ? QObject::tr("The SQLite file path is empty.") : QString();
    }
    if (host.trimmed().isEmpty()) {
        return QObject::tr("Enter the MySQL host.");
    }
    if (port < 1 || port > 65535) {
        return QObject::tr("Enter a port between 1 and 65535.");
    }
    if (databaseName.trimmed().isEmpty()) {
        return QObject::tr("Enter the database (schema) name.");
    }
    if (user.trimmed().isEmpty()) {
        return QObject::tr("Enter the MySQL user.");
    }
    return {};
}
