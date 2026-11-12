#include <QSqlQuery>
#include <QTest>

#include "Migrations.h"
#include "TestDatabase.h"

class MigrationsTest : public QObject
{
    Q_OBJECT

private slots:
    void splitsStatements();
    void bothBackendsHaveTheSameVersions();
    void appliesOnSqlite();
    void secondRunIsNoOp();
    void appliesOnMySql();
};

void MigrationsTest::splitsStatements()
{
    const QString sql = QStringLiteral("-- comment; with semicolon\n"
                                       "CREATE TABLE a (x TEXT DEFAULT 'a;b');\n"
                                       "/* block; comment */ INSERT INTO a VALUES (\"q;\");\n"
                                       "  ;\n"
                                       "SELECT 1");
    const QStringList statements = Migrations::splitStatements(sql);
    QCOMPARE(statements.size(), 3);
    QCOMPARE(statements.at(0), QStringLiteral("CREATE TABLE a (x TEXT DEFAULT 'a;b')"));
    QVERIFY(statements.at(1).startsWith(QStringLiteral("INSERT")));
    QCOMPARE(statements.at(2), QStringLiteral("SELECT 1"));
}

void MigrationsTest::bothBackendsHaveTheSameVersions()
{
    const auto sqlite = Migrations::load(QStringLiteral(":/migrations/sqlite"));
    const auto mysql = Migrations::load(QStringLiteral(":/migrations/mysql"));
    QVERIFY(!sqlite.empty());
    QCOMPARE(sqlite.size(), mysql.size());
    for (size_t i = 0; i < sqlite.size(); ++i) {
        QCOMPARE(sqlite[i].version, mysql[i].version);
        QCOMPARE(sqlite[i].name, mysql[i].name);
    }
}

void MigrationsTest::appliesOnSqlite()
{
    TestDatabase database;
    QVERIFY2(database.isReady(), qPrintable(database.migration().error));
    QVERIFY(database.migration().applied > 0);
    QSqlDatabase db = database.db();
    for (const char *table : {"sites", "jobs", "profile_snapshots", "speed_checks",
                              "discovery_runs", "mail_messages", "app_settings"}) {
        QVERIFY2(db.tables().contains(QString::fromLatin1(table)), table);
    }
}

void MigrationsTest::secondRunIsNoOp()
{
    TestDatabase database;
    QVERIFY(database.isReady());
    QSqlDatabase db = database.db();
    const Migrations::Result again = Migrations::apply(db, QStringLiteral(":/migrations/sqlite"));
    QVERIFY(again.ok);
    QCOMPARE(again.applied, 0);
    QCOMPARE(again.currentVersion, database.migration().currentVersion);
}

// Runs only when HYPERLANCER_TEST_MYSQL is set to "host;port;database;user;password". The
// database must be empty; the test drops its tables afterwards.
void MigrationsTest::appliesOnMySql()
{
    const QStringList parts = qEnvironmentVariable("HYPERLANCER_TEST_MYSQL").split(u';');
    if (parts.size() != 5) {
        QSKIP("Set HYPERLANCER_TEST_MYSQL=host;port;database;user;password to run.");
    }
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QMYSQL"))) {
        QSKIP("QMYSQL driver not available.");
    }
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), QStringLiteral("mysql-test"));
        db.setHostName(parts[0]);
        db.setPort(parts[1].toInt());
        db.setDatabaseName(parts[2]);
        db.setUserName(parts[3]);
        db.setPassword(parts[4]);
        QVERIFY(db.open());
        const Migrations::Result result =
            Migrations::apply(db, QStringLiteral(":/migrations/mysql"));
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(db.tables().contains(QStringLiteral("mail_messages")));
        QSqlQuery query(db);
        query.exec(QStringLiteral("SET FOREIGN_KEY_CHECKS = 0"));
        for (const QString &table : db.tables()) {
            query.exec(QStringLiteral("DROP TABLE `%1`").arg(table));
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("mysql-test"));
}

QTEST_GUILESS_MAIN(MigrationsTest)
#include "tst_migrations.moc"
