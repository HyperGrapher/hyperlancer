#include "Integrations.h"

#include <QDir>
#include <QUrl>

#include "AppSettings.h"
#include "Database.h"
#include "JobRunner.h"
#include "LangSearchClient.h"
#include "PageSpeedClient.h"
#include "SecretStore.h"

namespace {

const QString kPageSpeedKey = QStringLiteral("pagespeed/api-key");

QString localPath(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl.trimmed();
}

} // namespace

Integrations::Integrations(SecretStore &secrets, AppSettings &settings, Database &database,
                           JobRunner &runner, LangSearchClient &langSearch,
                           PageSpeedClient &pageSpeed, QString dataDir, QObject *parent)
    : QObject(parent)
    , m_secrets(secrets)
    , m_settings(settings)
    , m_database(database)
    , m_runner(runner)
    , m_langSearch(langSearch)
    , m_pageSpeed(pageSpeed)
    , m_dataDir(std::move(dataDir))
{
    connect(&m_settings, &AppSettings::toolPathsChanged, this, &Integrations::toolPathsChanged);
}

QString Integrations::hint(const QString &key)
{
    if (key.size() < 10) {
        return QStringLiteral("••••");
    }
    return key.left(4) + QStringLiteral("••••••") + key.right(4);
}

bool Integrations::hasLangSearchKey() const
{
    return !langSearchKey().isEmpty();
}

QString Integrations::langSearchKeyHint() const
{
    return hint(langSearchKey());
}

QString Integrations::langSearchKey() const
{
    return m_secrets.read(SecretKeys::kLangSearchApiKey).value_or(QString()).trimmed();
}

bool Integrations::hasPageSpeedKey() const
{
    return !pageSpeedKey().isEmpty();
}

QString Integrations::pageSpeedKeyHint() const
{
    return hint(pageSpeedKey());
}

QString Integrations::pageSpeedKey() const
{
    return m_secrets.read(kPageSpeedKey).value_or(QString()).trimmed();
}

QString Integrations::secretStoreName() const
{
    return SecretStore::backendName();
}

bool Integrations::secretStoreSecure() const
{
    return SecretStore::isSecureBackend();
}

QVariantMap Integrations::databaseSettings() const
{
    const DatabaseConfig config = m_settings.databaseConfig();
    return {
        {QStringLiteral("backend"),
         config.isMySql() ? QStringLiteral("mysql") : QStringLiteral("sqlite")},
        {QStringLiteral("host"), config.host},
        {QStringLiteral("port"), config.port},
        {QStringLiteral("database"), config.databaseName},
        {QStringLiteral("user"), config.user},
        {QStringLiteral("requireTls"), config.requireTls},
        {QStringLiteral("hasPassword"), m_secrets.read(SecretKeys::kMySqlPassword).has_value()}};
}

bool Integrations::mySqlDriverAvailable() const
{
    return Database::isDriverAvailable(DatabaseConfig::Backend::MySql);
}

QString Integrations::scriptsDir() const
{
    return QDir::toNativeSeparators(m_runner.toolchain().scriptsDir);
}

QString Integrations::pythonPath() const
{
    return QDir::toNativeSeparators(m_runner.toolchain().python);
}

bool Integrations::setLangSearchKey(const QString &key)
{
    if (key.trimmed().isEmpty() || !m_secrets.write(SecretKeys::kLangSearchApiKey, key.trimmed())) {
        return false;
    }
    emit keysChanged();
    return true;
}

void Integrations::removeLangSearchKey()
{
    m_secrets.remove(SecretKeys::kLangSearchApiKey);
    emit keysChanged();
}

void Integrations::testLangSearch()
{
    LangSearch::Request request;
    request.query = QStringLiteral("web design agency");
    request.count = 1;
    m_langSearch.search(langSearchKey(), request, [this](const LangSearch::Response &response) {
        if (response.error.isEmpty()) {
            emit testFinished(QStringLiteral("langsearch"), true,
                              tr("Key works. %n result(s) returned.", nullptr,
                                 static_cast<int>(response.hits.size())));
        } else {
            emit testFinished(QStringLiteral("langsearch"), false, response.error);
        }
    });
}

bool Integrations::setPageSpeedKey(const QString &key)
{
    if (const QString problem = PageSpeed::checkKeyFormat(key); !problem.isEmpty()) {
        emit testFinished(QStringLiteral("pagespeed"), false, problem);
        return false;
    }
    if (!m_secrets.write(kPageSpeedKey, key.trimmed())) {
        emit testFinished(QStringLiteral("pagespeed"), false,
                          tr("Could not save the key in %1.").arg(SecretStore::backendName()));
        return false;
    }
    emit keysChanged();
    return true;
}

void Integrations::removePageSpeedKey()
{
    m_secrets.remove(kPageSpeedKey);
    emit keysChanged();
}

void Integrations::testPageSpeed()
{
    m_pageSpeed.run(QStringLiteral("https://example.com/"), pageSpeedKey(), QStringLiteral("mobile"),
                    [this](const PageSpeed::Result &result) {
                        if (result.error.isEmpty()) {
                            emit testFinished(
                                QStringLiteral("pagespeed"), true,
                                tr("Works. example.com scored %1 on mobile.").arg(result.score));
                        } else {
                            emit testFinished(QStringLiteral("pagespeed"), false, result.error);
                        }
                    });
}

DatabaseConfig Integrations::configFrom(const QVariantMap &settings) const
{
    DatabaseConfig config;
    config.backend = settings.value(QStringLiteral("backend")).toString() == QStringLiteral("mysql")
                         ? DatabaseConfig::Backend::MySql
                         : DatabaseConfig::Backend::Sqlite;
    config.sqlitePath = QDir(m_dataDir).filePath(QStringLiteral("hyperlancer.sqlite"));
    config.host = settings.value(QStringLiteral("host")).toString().trimmed();
    config.port = settings.value(QStringLiteral("port"), 3306).toInt();
    config.databaseName = settings.value(QStringLiteral("database")).toString().trimmed();
    config.user = settings.value(QStringLiteral("user")).toString().trimmed();
    config.requireTls = settings.value(QStringLiteral("requireTls"), true).toBool();
    config.password = settings.value(QStringLiteral("password")).toString();
    if (config.password.isEmpty()) {
        config.password = m_secrets.read(SecretKeys::kMySqlPassword).value_or(QString());
    }
    return config;
}

void Integrations::testDatabase(const QVariantMap &settings)
{
    m_database.testConnection(
        configFrom(settings), this, [this](const Database::TestResult &result) {
            emit testFinished(QStringLiteral("database"), result.ok, result.message);
        });
}

void Integrations::applyDatabase(const QVariantMap &settings)
{
    const DatabaseConfig config = configFrom(settings);
    if (const QString invalid = config.validate(); !invalid.isEmpty()) {
        emit testFinished(QStringLiteral("database"), false, invalid);
        return;
    }
    const QString newPassword = settings.value(QStringLiteral("password")).toString();
    if (config.isMySql() && !newPassword.isEmpty() &&
        !m_secrets.write(SecretKeys::kMySqlPassword, newPassword)) {
        emit testFinished(QStringLiteral("database"), false,
                          tr("Could not save the password in %1.").arg(SecretStore::backendName()));
        return;
    }
    m_settings.setDatabaseConfig(config);
    emit databaseSettingsChanged();
    emit restartRequested();
}

void Integrations::setScriptsDir(const QString &path)
{
    m_settings.setScriptsDirOverride(QDir::fromNativeSeparators(localPath(path)));
}

void Integrations::setPythonPath(const QString &path)
{
    m_settings.setPythonOverride(QDir::fromNativeSeparators(localPath(path)));
}
