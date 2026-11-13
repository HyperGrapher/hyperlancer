#include "Migrations.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

#include "DbTime.h"

namespace Migrations {

namespace {

int recordedVersion(QSqlDatabase &db)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT MAX(version) FROM schema_migrations")) ||
        !query.next()) {
        return 0; // table does not exist yet
    }
    return query.value(0).toInt();
}

bool isSqlite(const QSqlDatabase &db)
{
    return db.driverName() == QStringLiteral("QSQLITE");
}

} // namespace

std::vector<Migration> load(const QString &dir)
{
    static const QRegularExpression pattern(QStringLiteral("^(\\d{4})_([A-Za-z0-9_]+)\\.sql$"));
    std::vector<Migration> migrations;
    const QStringList files = QDir(dir).entryList({QStringLiteral("*.sql")}, QDir::Files);
    for (const QString &fileName : files) {
        const QRegularExpressionMatch match = pattern.match(fileName);
        if (!match.hasMatch()) {
            continue;
        }
        QFile file(QDir(dir).filePath(fileName));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        migrations.push_back(
            {match.captured(1).toInt(), match.captured(2), QString::fromUtf8(file.readAll())});
    }
    std::sort(migrations.begin(), migrations.end(),
              [](const Migration &a, const Migration &b) { return a.version < b.version; });
    return migrations;
}

QStringList splitStatements(const QString &sql)
{
    QStringList statements;
    QString current;
    QChar quote;
    bool lineComment = false;
    bool blockComment = false;

    const auto flush = [&]() {
        const QString statement = current.trimmed();
        if (!statement.isEmpty()) {
            statements.append(statement);
        }
        current.clear();
    };

    for (qsizetype i = 0; i < sql.size(); ++i) {
        const QChar c = sql.at(i);
        const QChar next = i + 1 < sql.size() ? sql.at(i + 1) : QChar();
        if (lineComment) {
            if (c == u'\n') {
                lineComment = false;
                current.append(c);
            }
            continue;
        }
        if (blockComment) {
            if (c == u'*' && next == u'/') {
                blockComment = false;
                ++i;
            }
            continue;
        }
        if (!quote.isNull()) {
            current.append(c);
            if (c == quote) {
                quote = QChar();
            }
            continue;
        }
        if (c == u'-' && next == u'-') {
            lineComment = true;
            ++i;
            continue;
        }
        if (c == u'/' && next == u'*') {
            blockComment = true;
            ++i;
            continue;
        }
        if (c == u'\'' || c == u'"' || c == u'`') {
            quote = c;
            current.append(c);
            continue;
        }
        if (c == u';') {
            flush();
            continue;
        }
        current.append(c);
    }
    flush();
    return statements;
}

Result apply(QSqlDatabase &db, const QString &dir)
{
    Result result;
    const std::vector<Migration> migrations = load(dir);
    if (migrations.empty()) {
        result.error = QObject::tr("No migrations found in %1.").arg(dir);
        return result;
    }
    result.currentVersion = recordedVersion(db);

    for (const Migration &migration : migrations) {
        if (migration.version <= result.currentVersion) {
            continue;
        }
        const bool transactional = isSqlite(db);
        QSqlQuery query(db);
        if (transactional) {
            // A table rebuild must not trigger ON DELETE actions on the tables that point to it.
            // The pragma has no effect inside a transaction, so it comes first.
            query.exec(QStringLiteral("PRAGMA foreign_keys = OFF"));
            db.transaction();
        }
        const auto restoreForeignKeys = [&]() {
            if (transactional) {
                query.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
            }
        };
        for (const QString &statement : splitStatements(migration.sql)) {
            if (!query.exec(statement)) {
                if (transactional) {
                    db.rollback();
                }
                restoreForeignKeys();
                result.error =
                    QObject::tr("Migration %1 (%2) failed: %3\nStatement: %4")
                        .arg(migration.version, 4, 10, QChar(u'0'))
                        .arg(migration.name, query.lastError().text(), statement.left(300));
                return result;
            }
        }
        query.prepare(QStringLiteral(
            "INSERT INTO schema_migrations (version, name, applied_at) VALUES (?, ?, ?)"));
        query.addBindValue(migration.version);
        query.addBindValue(migration.name);
        query.addBindValue(DbTime::now());
        if (!query.exec()) {
            if (transactional) {
                db.rollback();
            }
            restoreForeignKeys();
            result.error = QObject::tr("Could not record migration %1: %2")
                               .arg(migration.version)
                               .arg(query.lastError().text());
            return result;
        }
        const bool committed = !transactional || db.commit();
        restoreForeignKeys();
        if (!committed) {
            result.error = QObject::tr("Could not commit migration %1: %2")
                               .arg(migration.version)
                               .arg(db.lastError().text());
            return result;
        }
        result.currentVersion = migration.version;
        ++result.applied;
    }
    result.ok = true;
    return result;
}

} // namespace Migrations
