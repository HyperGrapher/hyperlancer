#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "Database.h"

class DatabaseTest : public QObject
{
    Q_OBJECT

private slots:
    void runsWorkOnItsThreadAndReturnsResult();
    void skipsWorkWhenNotOpen();
    void reconnectsAfterServerClosedIdleConnection();

private:
    static bool waitReady(Database &database);
};

bool DatabaseTest::waitReady(Database &database)
{
    return QTest::qWaitFor([&]() { return database.state() != Database::State::Connecting; },
                           15000) &&
           database.state() == Database::State::Ready;
}

void DatabaseTest::runsWorkOnItsThreadAndReturnsResult()
{
    QTemporaryDir dir;
    DatabaseConfig config;
    config.sqlitePath = dir.filePath(QStringLiteral("t.sqlite"));
    Database database;
    database.start(config);
    QVERIFY2(waitReady(database), qPrintable(database.error()));

    QThread *workThread = nullptr;
    int count = -1;
    database.run(
        this,
        [&workThread](QSqlDatabase &db) {
            workThread = QThread::currentThread();
            QSqlQuery query(db);
            query.exec(QStringLiteral("SELECT COUNT(*) FROM sites"));
            return query.next() ? query.value(0).toInt() : -2;
        },
        [&count](int value) { count = value; });
    QTRY_COMPARE(count, 0);
    QVERIFY(workThread != QThread::currentThread());
}

void DatabaseTest::skipsWorkWhenNotOpen()
{
    DatabaseConfig config;
    config.backend = DatabaseConfig::Backend::MySql;
    config.host = QStringLiteral("127.0.0.1");
    config.port = 1; // nothing listens here
    config.databaseName = QStringLiteral("x");
    config.user = QStringLiteral("x");
    Database database;
    database.start(config);
    QVERIFY(QTest::qWaitFor([&]() { return database.state() != Database::State::Connecting; }, 15000));
    QCOMPARE(database.state(), Database::State::Failed);

    bool workRan = false;
    bool done = false;
    database.run(
        this,
        [&workRan](QSqlDatabase &) {
            workRan = true;
            return 1;
        },
        [&done](int) { done = true; });
    QTRY_VERIFY(done);
    QVERIFY(!workRan);
}

// The "MySQL server has gone away" case: the server closes a connection that sat idle longer
// than its wait_timeout. Runs only with HYPERLANCER_TEST_MYSQL=host;port;database;user;password.
void DatabaseTest::reconnectsAfterServerClosedIdleConnection()
{
    const QStringList parts = qEnvironmentVariable("HYPERLANCER_TEST_MYSQL").split(u';');
    if (parts.size() != 5) {
        QSKIP("Set HYPERLANCER_TEST_MYSQL=host;port;database;user;password to run.");
    }
    if (!Database::isDriverAvailable(DatabaseConfig::Backend::MySql)) {
        QSKIP("QMYSQL driver not available.");
    }
    DatabaseConfig config;
    config.backend = DatabaseConfig::Backend::MySql;
    config.host = parts[0];
    config.port = parts[1].toInt();
    config.databaseName = parts[2];
    config.user = parts[3];
    config.password = parts[4];
    config.requireTls = false;
    Database database;
    database.start(config);
    QVERIFY2(waitReady(database), qPrintable(database.error()));

    bool shortened = false;
    database.run(
        this,
        [](QSqlDatabase &db) {
            return QSqlQuery(db).exec(QStringLiteral("SET SESSION wait_timeout = 2"));
        },
        [&shortened](bool ok) { shortened = ok; });
    QTRY_VERIFY(shortened);

    QTest::qWait(7000); // longer than wait_timeout and the idle check interval

    QString error = QStringLiteral("not run");
    database.run(
        this,
        [](QSqlDatabase &db) {
            QSqlQuery query(db);
            return query.exec(QStringLiteral("SELECT COUNT(*) FROM sites"))
                       ? QString()
                       : query.lastError().text();
        },
        [&error](const QString &value) { error = value; });
    QTRY_VERIFY_WITH_TIMEOUT(error != QStringLiteral("not run"), 15000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

QTEST_GUILESS_MAIN(DatabaseTest)
#include "tst_database.moc"
