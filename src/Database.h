#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QSqlDatabase>
#include <QThread>

#include "DatabaseConfig.h"

// Owns the one database connection and the thread it lives on. No query ever runs on the UI
// thread: callers hand a work function to run(), which executes it on the database thread
// and delivers the result back on the UI thread. One thread also gives SQLite the single
// writer it wants, and keeps a slow remote MySQL server from freezing the window.
class Database : public QObject
{
    Q_OBJECT

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString description READ description NOTIFY stateChanged)
    Q_PROPERTY(bool mySql READ isMySql NOTIFY stateChanged)
    Q_PROPERTY(int schemaVersion READ schemaVersion NOTIFY stateChanged)

public:
    enum class State
    {
        Connecting,
        Ready,
        Failed
    };
    Q_ENUM(State)

    struct TestResult
    {
        bool ok = false;
        QString message;
    };

    explicit Database(QObject *parent = nullptr);
    ~Database() override;

    // Opens the connection and applies migrations on the database thread. Emits stateChanged
    // when done; ready() follows on success.
    void start(const DatabaseConfig &config);

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] QString description() const { return m_config.describe(); }
    [[nodiscard]] bool isMySql() const { return m_config.isMySql(); }
    [[nodiscard]] int schemaVersion() const { return m_schemaVersion; }
    [[nodiscard]] const DatabaseConfig &config() const { return m_config; }

    // Runs work(QSqlDatabase &) on the database thread, then done(result) on this object's
    // thread. done is skipped when context was destroyed in the meantime. The result type
    // must be copyable and should carry its own error text: work must not throw.
    template <typename Work, typename Done> void run(QObject *context, Work work, Done done);

    // Same, without a result.
    template <typename Work> void post(Work work);

    // Blocks the calling thread until work has run. For start-up, shutdown and tests only.
    template <typename Work>
    auto runBlocking(Work work) -> std::invoke_result_t<Work, QSqlDatabase &>;

    // Connects with a separate, short-lived connection and runs SELECT VERSION(). Used by the
    // settings page before switching databases. done runs on this object's thread.
    void testConnection(const DatabaseConfig &config, QObject *context,
                        std::function<void(TestResult)> done);

    [[nodiscard]] static bool isDriverAvailable(DatabaseConfig::Backend backend);

signals:
    void stateChanged();
    void ready();

private:
    // Lives on m_thread. Only ever touched from there, so it needs no locking.
    QSqlDatabase &connection();
    void openOnThread(const DatabaseConfig &config);
    void finishStart(bool ok, const QString &error, int schemaVersion);

    QThread m_thread;
    std::unique_ptr<QObject> m_worker;
    QString m_connectionName;
    QSqlDatabase m_connection;
    QDateTime m_lastUse;
    DatabaseConfig m_config;
    State m_state = State::Connecting;
    QString m_error;
    int m_schemaVersion = 0;
};

template <typename Work, typename Done> void Database::run(QObject *context, Work work, Done done)
{
    QPointer<QObject> guard(context);
    QMetaObject::invokeMethod(
        m_worker.get(),
        [this, guard, work = std::move(work), done = std::move(done)]() mutable {
            // Without an open connection (start-up failed) the work is skipped and done gets
            // a default result; the UI shows the database error instead.
            using Result = std::invoke_result_t<Work, QSqlDatabase &>;
            Result result{};
            if (m_connection.isOpen()) {
                result = work(connection());
                m_lastUse = QDateTime::currentDateTimeUtc();
            }
            // Hop back through this object (always alive while the thread runs) and check the
            // guard there, on the thread that owns the context.
            QMetaObject::invokeMethod(
                this,
                [guard, done = std::move(done), result = std::move(result)]() mutable {
                    if (guard) {
                        done(std::move(result));
                    }
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

template <typename Work> void Database::post(Work work)
{
    QMetaObject::invokeMethod(
        m_worker.get(),
        [this, work = std::move(work)]() mutable {
            if (m_connection.isOpen()) {
                work(connection());
                m_lastUse = QDateTime::currentDateTimeUtc();
            }
        },
        Qt::QueuedConnection);
}

template <typename Work>
auto Database::runBlocking(Work work) -> std::invoke_result_t<Work, QSqlDatabase &>
{
    using Result = std::invoke_result_t<Work, QSqlDatabase &>;
    if constexpr (std::is_void_v<Result>) {
        QMetaObject::invokeMethod(
            m_worker.get(), [this, &work]() { work(connection()); }, Qt::BlockingQueuedConnection);
    } else {
        Result result{};
        QMetaObject::invokeMethod(
            m_worker.get(), [this, &work, &result]() { result = work(connection()); },
            Qt::BlockingQueuedConnection);
        return result;
    }
}
