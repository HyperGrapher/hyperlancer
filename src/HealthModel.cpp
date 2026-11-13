#include "HealthModel.h"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>

#include "AppPaths.h"
#include "Database.h"
#include "GmailService.h"
#include "Integrations.h"
#include "JobRunner.h"
#include "SecretStore.h"

HealthModel::HealthModel(const AppPaths &paths, Database &database, JobRunner &runner,
                         Integrations &integrations, GmailService &gmail, QObject *parent)
    : QObject(parent)
    , m_paths(paths)
    , m_database(database)
    , m_runner(runner)
    , m_integrations(integrations)
    , m_gmail(gmail)
{
    connect(&m_database, &Database::stateChanged, this, &HealthModel::refresh);
    connect(&m_gmail, &GmailService::stateChanged, this, &HealthModel::refresh);
    connect(&m_integrations, &Integrations::keysChanged, this, &HealthModel::refresh);
    connect(&m_integrations, &Integrations::toolPathsChanged, this, &HealthModel::refresh);
}

int HealthModel::problemCount() const
{
    int count = 0;
    for (const QVariant &check : m_checks) {
        if (check.toMap().value(QStringLiteral("state")).toString() == QStringLiteral("error")) {
            ++count;
        }
    }
    return count;
}

void HealthModel::setCheck(const QString &id, const QString &title, const QString &state,
                           const QString &detail, const QString &hint)
{
    const QVariantMap check{{QStringLiteral("id"), id},
                            {QStringLiteral("title"), title},
                            {QStringLiteral("state"), state},
                            {QStringLiteral("detail"), detail},
                            {QStringLiteral("hint"), hint}};
    for (QVariant &existing : m_checks) {
        if (existing.toMap().value(QStringLiteral("id")).toString() == id) {
            existing = check;
            emit checksChanged();
            return;
        }
    }
    m_checks.append(check);
    emit checksChanged();
}

void HealthModel::refresh()
{
    switch (m_database.state()) {
    case Database::State::Connecting:
        setCheck(QStringLiteral("database"), tr("Database"), QStringLiteral("checking"),
                 m_database.description());
        break;
    case Database::State::Ready:
        setCheck(QStringLiteral("database"), tr("Database"), QStringLiteral("ok"),
                 tr("%1 · schema version %2")
                     .arg(m_database.description())
                     .arg(m_database.schemaVersion()));
        break;
    case Database::State::Failed:
        setCheck(QStringLiteral("database"), tr("Database"), QStringLiteral("error"),
                 m_database.error(),
                 m_database.isMySql()
                     ? tr("Check host, port, user and password in Connections, and that the server "
                          "accepts remote connections from this IP.")
                     : tr("Check that the data folder is writable."));
        break;
    }

    const bool mysqlDriver = m_integrations.mySqlDriverAvailable();
    setCheck(QStringLiteral("mysql-driver"), tr("MySQL driver"),
             mysqlDriver ? QStringLiteral("ok") : QStringLiteral("warn"),
             mysqlDriver ? tr("Qt MySQL plugin found")
                         : tr("Qt MySQL plugin (qsqlmysql) not installed"),
             mysqlDriver ? QString()
                         : tr("Only needed for a remote MySQL database. See README → \"MySQL "
                              "driver on Windows\"."));

    const bool writable = m_paths.isWritable();
    setCheck(QStringLiteral("data-folder"), tr("Data folder"),
             writable ? QStringLiteral("ok") : QStringLiteral("error"),
             QDir::toNativeSeparators(m_paths.dataDir),
             writable ? QString() : tr("The app cannot write here. Check folder permissions."));

    setCheck(QStringLiteral("secrets"), tr("Credential storage"),
             SecretStore::isSecureBackend() ? QStringLiteral("ok") : QStringLiteral("warn"),
             SecretStore::backendName(),
             SecretStore::isSecureBackend() ? QString()
                                            : tr("Keys are stored unencrypted on this platform."));

    setCheck(QStringLiteral("langsearch"), tr("LangSearch API key"),
             m_integrations.hasLangSearchKey() ? QStringLiteral("ok") : QStringLiteral("warn"),
             m_integrations.hasLangSearchKey() ? m_integrations.langSearchKeyHint() : tr("Not set"),
             m_integrations.hasLangSearchKey() ? QString()
                                               : tr("Needed for Discover. Add it in Connections."));

    const bool pageSpeedKey = m_integrations.hasPageSpeedKey();
    setCheck(QStringLiteral("pagespeed"), tr("PageSpeed Insights"),
             pageSpeedKey ? QStringLiteral("ok") : QStringLiteral("warn"),
             pageSpeedKey ? tr("API key %1").arg(m_integrations.pageSpeedKeyHint())
                          : tr("No key: anonymous calls with a small shared quota"),
             pageSpeedKey ? QString() : tr("Add an API key in Connections for reliable quota."));

    setCheck(QStringLiteral("gmail"), tr("Gmail"),
             m_gmail.isConnected()          ? QStringLiteral("ok")
             : m_gmail.isClientConfigured() ? QStringLiteral("warn")
                                            : QStringLiteral("warn"),
             m_gmail.status(),
             m_gmail.isConnected()
                 ? QString()
                 : tr("Import the OAuth client file and connect in Connections."));

    checkPythonTools();
}

void HealthModel::checkPythonTools()
{
    const JobRunner::Toolchain tools = m_runner.toolchain();
    if (tools.scriptsDir.isEmpty() || !QFileInfo::exists(tools.scriptsDir)) {
        setCheck(
            QStringLiteral("python"), tr("Python tools"), QStringLiteral("error"),
            tr("Tools folder not found"),
            tr("Set the folder that contains pyproject.toml (tools) in Tools."));
        return;
    }
    if (!QFileInfo(tools.python).isExecutable()) {
        setCheck(QStringLiteral("python"), tr("Python tools"), QStringLiteral("error"),
                 tr("No Python at %1").arg(QDir::toNativeSeparators(tools.python)),
                 tr("Install uv, then run: uv sync --project \"%1\"")
                     .arg(QDir::toNativeSeparators(tools.scriptsDir)));
        return;
    }
    setCheck(QStringLiteral("python"), tr("Python tools"), QStringLiteral("checking"),
             QDir::toNativeSeparators(tools.python));
    auto *process = new QProcess(this);
    process->setWorkingDirectory(tools.scriptsDir);
    process->setProgram(tools.python);
    process->setArguments(
        {QStringLiteral("-c"), QStringLiteral("import sys, site_profile.summary, site_tools; "
                                              "print(sys.version.split()[0])")});
    QPointer<HealthModel> self(this);
    connect(
        process, &QProcess::finished, this,
        [self, process, python = tools.python](int exitCode, QProcess::ExitStatus status) {
            process->deleteLater();
            if (!self) {
                return;
            }
            const QString version = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
            if (status == QProcess::NormalExit && exitCode == 0) {
                self->setCheck(QStringLiteral("python"), tr("Python tools"), QStringLiteral("ok"),
                               tr("Python %1 · %2").arg(version, QDir::toNativeSeparators(python)));
            } else {
                self->setCheck(
                    QStringLiteral("python"), tr("Python tools"), QStringLiteral("error"),
                    QString::fromUtf8(process->readAllStandardError()).trimmed().right(300),
                    tr("Run: uv sync --project <tools folder>"));
            }
        });
    connect(process, &QProcess::errorOccurred, this, [self, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        process->deleteLater();
        if (self) {
            self->setCheck(QStringLiteral("python"), tr("Python tools"), QStringLiteral("error"),
                           process->errorString());
        }
    });
    process->start();
}
