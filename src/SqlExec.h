#pragma once

#include <initializer_list>

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

// Small helpers for bound-parameter queries. Every query in the app goes through these, so
// failures are logged in one place and values are never spliced into SQL text.
namespace SqlExec {

// Prepares, binds positionally and executes. On failure logs the error, stores it in *error
// (if given) and returns a query whose isActive() is false.
inline QSqlQuery run(QSqlDatabase &db, const QString &sql,
                     std::initializer_list<QVariant> binds = {}, QString *error = nullptr)
{
    QSqlQuery query(db);
    query.setForwardOnly(true);
    if (!query.prepare(sql)) {
        const QString text = query.lastError().text();
        qWarning("SQL prepare failed: %s | %s", qPrintable(text), qPrintable(sql.left(200)));
        if (error) {
            *error = text;
        }
        return query;
    }
    for (const QVariant &value : binds) {
        query.addBindValue(value);
    }
    if (!query.exec()) {
        const QString text = query.lastError().text();
        qWarning("SQL failed: %s | %s", qPrintable(text), qPrintable(sql.left(200)));
        if (error) {
            *error = text;
        }
    }
    return query;
}

inline bool isMySql(const QSqlDatabase &db)
{
    return db.driverName() == QStringLiteral("QMYSQL");
}

// "INSERT OR IGNORE" (SQLite) or "INSERT IGNORE" (MySQL).
inline QString insertIgnore(const QSqlDatabase &db)
{
    return isMySql(db) ? QStringLiteral("INSERT IGNORE") : QStringLiteral("INSERT OR IGNORE");
}

// NULL for an empty string, so optional text columns stay NULL instead of ''.
inline QVariant textOrNull(const QString &value)
{
    return value.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(value);
}

inline QVariant nullInt()
{
    return QVariant(QMetaType(QMetaType::LongLong));
}

// RAII transaction: rolls back unless commit() succeeded.
class Transaction
{
public:
    explicit Transaction(QSqlDatabase &db)
        : m_db(db)
        , m_active(db.transaction())
    {}
    ~Transaction()
    {
        if (m_active) {
            m_db.rollback();
        }
    }
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;

    [[nodiscard]] bool isActive() const { return m_active; }
    bool commit()
    {
        if (!m_active) {
            return false;
        }
        m_active = false;
        return m_db.commit();
    }

private:
    QSqlDatabase &m_db;
    bool m_active;
};

} // namespace SqlExec
