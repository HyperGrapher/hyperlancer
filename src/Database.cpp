#include "Database.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include "Migrations.h"

namespace {

// A remote connection that sat idle this long is checked with a cheap query before use, so a
// server-side idle timeout ("MySQL server has gone away") turns into a reconnect instead of a
// failed query. Hosted servers often close idle connections after 30-60 seconds or less, so
// the check runs after any pause; during a burst of queries it is skipped.
constexpr qint64 kIdleCheckMs = 5000;

void applyOptions(QSqlDatabase &db, const DatabaseConfig &config)
{
    if (config.isMySql()) {
        db.setHostName(config.host.trimmed());
        db.setPort(config.port);
        db.setDatabaseName(config.databaseName.trimmed());
        db.setUserName(config.user.trimmed());
        db.setPassword(config.password);
        QString options = QStringLiteral(
            "MYSQL_OPT_CONNECT_TIMEOUT=10;MYSQL_OPT_READ_TIMEOUT=60;MYSQL_OPT_WRITE_TIMEOUT=60");
        if (config.requireTls) {
            options += QStringLiteral(";MYSQL_OPT_SSL_MODE=SSL_MODE_REQUIRED");
        }
        db.setConnectOptions(options);
    } else {
        db.setDatabaseName(config.sqlitePath);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    }
}

QString initSession(QSqlDatabase &db, const DatabaseConfig &config)
{
    QSqlQuery query(db);
    const QStringList statements = config.isMySql()
                                       ? QStringList{QStringLiteral("SET NAMES utf8mb4"),
                                                     QStringLiteral("SET time_zone = '+00:00'")}
                                       : QStringList{QStringLiteral("PRAGMA foreign_keys = ON"),
                                                     QStringLiteral("PRAGMA journal_mode = WAL"),
                                                     QStringLiteral("PRAGMA synchronous = NORMAL")};
    for (const QString &statement : statements) {
        if (!query.exec(statement)) {
            return query.lastError().text();
        }
    }
    return {};
}

} // namespace

Database::Database(QObject *parent)
    : QObject(parent)
    , m_worker(std::make_unique<QObject>())
    , m_connectionName(QStringLiteral("main-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_thread.setObjectName(QStringLiteral("database"));
    m_worker->moveToThread(&m_thread);
    m_thread.start();
}

Database::~Database()
{
    QMetaObject::invokeMethod(
        m_worker.get(),
        [this]() {
            m_connection.close();
            m_connection = QSqlDatabase();
            QSqlDatabase::removeDatabase(m_connectionName);
        },
        Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

void Database::start(const DatabaseConfig &config)
{
    m_config = config;
    m_state = State::Connecting;
    m_error.clear();
    emit stateChanged();
    // The password stays only in the thread-side copy.
    m_config.password.clear();
    QMetaObject::invokeMethod(
        m_worker.get(), [this, config]() { openOnThread(config); }, Qt::QueuedConnection);
}

void Database::openOnThread(const DatabaseConfig &config)
{
    if (m_connection.isValid()) {
        m_connection.close();
        m_connection = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    if (!QSqlDatabase::isDriverAvailable(config.driverName())) {
        const QString error =
            tr("The %1 driver is not installed with this Qt build.").arg(config.driverName());
        QMetaObject::invokeMethod(this, [this, error]() { finishStart(false, error, 0); });
        return;
    }
    m_connection = QSqlDatabase::addDatabase(config.driverName(), m_connectionName);
    applyOptions(m_connection, config);

    QString error;
    int version = 0;
    if (!m_connection.open()) {
        error = tr("Could not connect: %1").arg(m_connection.lastError().text());
    } else if (const QString sessionError = initSession(m_connection, config);
               !sessionError.isEmpty()) {
        error = tr("Could not prepare the connection: %1").arg(sessionError);
    } else {
        const Migrations::Result migrated = Migrations::apply(m_connection, config.migrationsDir());
        if (!migrated.ok) {
            error = migrated.error;
        }
        version = migrated.currentVersion;
    }
    m_lastUse = QDateTime::currentDateTimeUtc();
    QMetaObject::invokeMethod(
        this, [this, error, version]() { finishStart(error.isEmpty(), error, version); });
}

void Database::finishStart(bool ok, const QString &error, int schemaVersion)
{
    m_state = ok ? State::Ready : State::Failed;
    m_error = error;
    m_schemaVersion = schemaVersion;
    if (!ok) {
        qWarning("Database start failed: %s", qPrintable(error));
    }
    emit stateChanged();
    if (ok) {
        emit ready();
    }
}

QSqlDatabase &Database::connection()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (m_connection.isValid() && m_connection.driverName() == QStringLiteral("QMYSQL") &&
        m_lastUse.msecsTo(now) > kIdleCheckMs) {
        QSqlQuery ping(m_connection);
        if (!ping.exec(QStringLiteral("SELECT 1"))) {
            qWarning("Database connection lost (%s), reconnecting",
                     qPrintable(ping.lastError().text()));
            ping.finish();
            m_connection.close();
            if (m_connection.open()) {
                const QString sessionError = initSession(m_connection, m_config);
                if (!sessionError.isEmpty()) {
                    qWarning("Database reconnected, but session setup failed: %s",
                             qPrintable(sessionError));
                } else {
                    qInfo("Database reconnected");
                }
            } else {
                qWarning("Database reconnect failed: %s",
                         qPrintable(m_connection.lastError().text()));
            }
        }
    }
    m_lastUse = QDateTime::currentDateTimeUtc();
    return m_connection;
}

void Database::testConnection(const DatabaseConfig &config, QObject *context,
                              std::function<void(TestResult)> done)
{
    QPointer<QObject> guard(context);
    auto future = QtConcurrent::run([config]() {
        TestResult result;
        if (const QString invalid = config.validate(); !invalid.isEmpty()) {
            result.message = invalid;
            return result;
        }
        if (!QSqlDatabase::isDriverAvailable(config.driverName())) {
            result.message = QObject::tr("The %1 driver is not installed with this Qt build.")
                                 .arg(config.driverName());
            return result;
        }
        const QString name =
            QStringLiteral("test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(config.driverName(), name);
            applyOptions(db, config);
            if (!db.open()) {
                result.message = db.lastError().text();
            } else {
                QSqlQuery query(db);
                const QString versionSql = config.isMySql()
                                               ? QStringLiteral("SELECT VERSION()")
                                               : QStringLiteral("SELECT sqlite_version()");
                if (query.exec(versionSql) && query.next()) {
                    result.ok = true;
                    result.message =
                        QObject::tr("Connected. Server version %1.").arg(query.value(0).toString());
                } else {
                    result.message = query.lastError().text();
                }
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
        return result;
    });
    future.then(this, [guard, done = std::move(done)](TestResult result) {
        if (guard) {
            done(result);
        }
    });
}

bool Database::isDriverAvailable(DatabaseConfig::Backend backend)
{
    DatabaseConfig config;
    config.backend = backend;
    return QSqlDatabase::isDriverAvailable(config.driverName());
}
