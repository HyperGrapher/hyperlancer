#pragma once

#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "DatabaseConfig.h"

class AppSettings;
class Database;
class JobRunner;
class LangSearchClient;
class PageSpeedClient;
class SecretStore;

// Backs Settings → Connections: API keys (stored in the OS credential store, replaceable at
// any time), the database connection, and the Python tool paths.
class Integrations : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Integrations is created by the application.")

    Q_PROPERTY(bool hasLangSearchKey READ hasLangSearchKey NOTIFY keysChanged)
    Q_PROPERTY(QString langSearchKeyHint READ langSearchKeyHint NOTIFY keysChanged)
    Q_PROPERTY(bool hasPageSpeedKey READ hasPageSpeedKey NOTIFY keysChanged)
    Q_PROPERTY(QString pageSpeedKeyHint READ pageSpeedKeyHint NOTIFY keysChanged)
    Q_PROPERTY(QString secretStoreName READ secretStoreName CONSTANT)
    Q_PROPERTY(bool secretStoreSecure READ secretStoreSecure CONSTANT)
    Q_PROPERTY(QVariantMap databaseSettings READ databaseSettings NOTIFY databaseSettingsChanged)
    Q_PROPERTY(bool mySqlDriverAvailable READ mySqlDriverAvailable CONSTANT)
    Q_PROPERTY(QString scriptsDir READ scriptsDir NOTIFY toolPathsChanged)
    Q_PROPERTY(QString pythonPath READ pythonPath NOTIFY toolPathsChanged)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)

public:
    Integrations(SecretStore &secrets, AppSettings &settings, Database &database, JobRunner &runner,
                 LangSearchClient &langSearch, PageSpeedClient &pageSpeed, QString dataDir,
                 QObject *parent = nullptr);

    [[nodiscard]] bool hasLangSearchKey() const;
    [[nodiscard]] QString langSearchKeyHint() const;
    [[nodiscard]] QString langSearchKey() const;
    [[nodiscard]] bool hasPageSpeedKey() const;
    [[nodiscard]] QString pageSpeedKeyHint() const;
    [[nodiscard]] QString pageSpeedKey() const;
    [[nodiscard]] QString secretStoreName() const;
    [[nodiscard]] bool secretStoreSecure() const;
    [[nodiscard]] QVariantMap databaseSettings() const;
    [[nodiscard]] bool mySqlDriverAvailable() const;
    [[nodiscard]] QString scriptsDir() const;
    [[nodiscard]] QString pythonPath() const;
    [[nodiscard]] QString dataDir() const { return m_dataDir; }

    // Saves (or replaces) a key. An empty value is refused; use remove instead.
    Q_INVOKABLE bool setLangSearchKey(const QString &key);
    Q_INVOKABLE void removeLangSearchKey();
    // One small real search; emits testFinished("langsearch", ok, message).
    Q_INVOKABLE void testLangSearch();
    Q_INVOKABLE bool setPageSpeedKey(const QString &key);
    Q_INVOKABLE void removePageSpeedKey();
    Q_INVOKABLE void testPageSpeed();

    // settings: backend ("sqlite"|"mysql"), host, port, database, user, password (empty keeps
    // the stored one), requireTls. Emits testFinished("database", ok, message).
    Q_INVOKABLE void testDatabase(const QVariantMap &settings);
    // Saves the settings and the password, then restarts the app on the new database.
    Q_INVOKABLE void applyDatabase(const QVariantMap &settings);

    Q_INVOKABLE void setScriptsDir(const QString &path);
    Q_INVOKABLE void setPythonPath(const QString &path);

signals:
    void keysChanged();
    void databaseSettingsChanged();
    void toolPathsChanged();
    void testFinished(const QString &service, bool ok, const QString &message);
    void restartRequested();

private:
    [[nodiscard]] static QString hint(const QString &key);
    [[nodiscard]] DatabaseConfig configFrom(const QVariantMap &settings) const;

    SecretStore &m_secrets;
    AppSettings &m_settings;
    Database &m_database;
    JobRunner &m_runner;
    LangSearchClient &m_langSearch;
    PageSpeedClient &m_pageSpeed;
    QString m_dataDir;
};
