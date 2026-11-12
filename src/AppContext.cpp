#include "AppContext.h"

#include <utility>
#include <vector>

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include "AppSettings.h"
#include "PublicSuffixList.h"
#include "SiteStore.h"
#include "SqlExec.h"
#include "UrlNormalizer.h"

AppContext::AppContext(const AppPaths &paths, AppSettings &settings, QObject *parent)
    : QObject(parent)
    , m_paths(paths)
    , m_settings(settings)
    , m_sounds(settings)
    , m_secrets(QDir(paths.dataDir).filePath(QStringLiteral("secrets-dev.ini")))
    , m_runner(m_database, m_paths, m_settings)
    , m_langSearch(m_network)
    , m_pageSpeed(m_network)
    , m_integrations(m_secrets, m_settings, m_database, m_runner, m_langSearch, m_pageSpeed,
                     m_paths.dataDir)
    , m_gmail(m_secrets, m_database, m_network)
    , m_inbox(m_gmail, m_database, m_network)
    , m_sites(m_database, m_runner)
    , m_jobs(m_database, m_runner)
    , m_siteDetail(m_database, m_runner, m_pageSpeed, m_integrations, m_gmail, paths.dataDir)
    , m_dashboard(m_database, m_runner)
    , m_discovery(m_database, m_runner, m_langSearch, m_integrations)
    , m_health(m_paths, m_database, m_runner, m_integrations, m_gmail)
{
    m_gmail.setChromeProfile(m_settings.gmailChromeProfile());
    connect(&m_settings, &AppSettings::gmailChromeProfileChanged, this,
            [this]() { m_gmail.setChromeProfile(m_settings.gmailChromeProfile()); });
    connect(&m_database, &Database::ready, this, &AppContext::onDatabaseReady);
    connect(&m_runner, &JobRunner::notice, this, &AppContext::notice);
    connect(&m_gmail, &GmailService::notice, this, &AppContext::notice);
    // Sounds for the moments the user cares about; which sound each plays is a setting.
    connect(&m_inbox, &GmailInbox::newMessages, &m_sounds,
            [this](const QVariantList &) { m_sounds.playEvent(QStringLiteral("mailArrived")); });
    connect(&m_runner, &JobRunner::jobFinished, &m_sounds,
            [this](qint64, qint64, const QString &status) {
                if (status == QStringLiteral("failed")) {
                    m_sounds.playEvent(QStringLiteral("analysisFailed"));
                } else if (status != QStringLiteral("canceled")) {
                    m_sounds.playEvent(QStringLiteral("analysisDone"));
                }
            });
    connect(&m_siteDetail, &SiteDetail::speedCheckFinished, &m_sounds, [this](bool ok) {
        m_sounds.playEvent(ok ? QStringLiteral("speedCheckDone") : QStringLiteral("error"));
    });
    connect(&m_discovery, &Discovery::searchFinished, &m_sounds, [this](bool ok) {
        m_sounds.playEvent(ok ? QStringLiteral("discoveryDone") : QStringLiteral("error"));
    });
    connect(this, &AppContext::notice, &m_sounds, [this](const QString &, bool isError) {
        if (isError) {
            m_sounds.playEvent(QStringLiteral("error"));
        }
    });
    connect(&m_gmail, &GmailService::siteMailChanged, &m_sites, &SiteListModel::refreshSoon);
    connect(&m_inbox, &GmailInbox::siteMailChanged, &m_sites, &SiteListModel::refreshSoon);
    connect(&m_siteDetail, &SiteDetail::notice, this, &AppContext::notice);
    connect(&m_siteDetail, &SiteDetail::organizerChanged, &m_sites, &SiteListModel::refreshSoon);
    connect(&m_discovery, &Discovery::notice, this, &AppContext::notice);
    connect(&m_integrations, &Integrations::restartRequested, this, &AppContext::restart);
}

AppContext::~AppContext() = default;

void AppContext::start()
{
    m_health.refresh();
    m_database.start(databaseConfig());
}

DatabaseConfig AppContext::databaseConfig() const
{
    DatabaseConfig config = m_settings.databaseConfig();
    config.sqlitePath = m_paths.sqlitePath;
    if (config.isMySql()) {
        config.password = m_secrets.read(SecretKeys::kMySqlPassword).value_or(QString());
    }
    return config;
}

void AppContext::onDatabaseReady()
{
    m_runner.start();
    m_gmail.start();
    m_sites.refresh();
    m_jobs.refresh();
    m_dashboard.refresh();
    m_discovery.loadRecentRuns();
}

QString AppContext::version() const
{
    return QCoreApplication::applicationVersion();
}

void AppContext::addSite(const QString &text, bool analyze)
{
    const UrlNormalizeResult normalized = normalizeUrl(text, PublicSuffixList::bundled());
    if (!normalized.ok()) {
        emit notice(normalized.error, true);
        return;
    }
    const NormalizedUrl url = normalized.value;
    m_database.run(
        this,
        [url](QSqlDatabase &db) { return SiteStore::addSite(db, url, QStringLiteral("manual")); },
        [this, url, analyze](const SiteStore::AddResult &result) {
            if (!result.error.isEmpty()) {
                emit notice(tr("Could not add the site: %1").arg(result.error), true);
                return;
            }
            if (analyze) {
                m_runner.enqueueProfile(result.siteId, url.url);
            }
            m_sites.refreshSoon();
            emit siteAdded(result.siteId, result.created);
            if (!result.created) {
                emit notice(tr("%1 is already in your sites.").arg(url.siteKey), false);
            }
        });
}

void AppContext::addSites(const QString &text, bool analyze)
{
    struct Outcome
    {
        int added = 0;
        int existing = 0;
        std::vector<std::pair<qint64, QString>> created; // site id and URL, to analyze
    };

    QStringList invalid;
    std::vector<NormalizedUrl> urls;
    QSet<QString> seen;
    static const QRegularExpression separators(QStringLiteral("[\\s,;]+"));
    for (const QString &token : text.split(separators, Qt::SkipEmptyParts)) {
        const UrlNormalizeResult normalized = normalizeUrl(token, PublicSuffixList::bundled());
        if (!normalized.ok()) {
            invalid.append(token);
        } else if (!seen.contains(normalized.value.siteKey)) {
            seen.insert(normalized.value.siteKey);
            urls.push_back(normalized.value);
        }
    }
    if (urls.empty()) {
        emit sitesAdded(0, 0, invalid);
        return;
    }
    m_database.run(
        this,
        [urls](QSqlDatabase &db) {
            Outcome outcome;
            SqlExec::Transaction transaction(db);
            for (const NormalizedUrl &url : urls) {
                const SiteStore::AddResult result =
                    SiteStore::addSite(db, url, QStringLiteral("manual"));
                if (!result.error.isEmpty()) {
                    continue;
                }
                if (result.created) {
                    ++outcome.added;
                    outcome.created.emplace_back(result.siteId, url.url);
                } else {
                    ++outcome.existing;
                }
            }
            transaction.commit();
            return outcome;
        },
        [this, analyze, invalid](const Outcome &outcome) {
            if (analyze) {
                for (const auto &[siteId, url] : outcome.created) {
                    m_runner.enqueueProfile(siteId, url);
                }
            }
            m_sites.refresh();
            m_dashboard.refresh();
            emit sitesAdded(outcome.added, outcome.existing, invalid);
        });
}

void AppContext::removeSites(const QVariantList &siteIds)
{
    for (const QVariant &id : siteIds) {
        removeSite(id.toLongLong());
    }
}

void AppContext::analyzeSite(qint64 siteId, const QString &url)
{
    m_runner.enqueueProfile(siteId, url, [this](qint64, const QString &error) {
        if (!error.isEmpty()) {
            emit notice(error, true);
        }
    });
}

void AppContext::removeSite(qint64 siteId)
{
    m_database.run(
        this, [siteId](QSqlDatabase &db) { return SiteStore::deleteSite(db, siteId); },
        [this, siteId](bool removed) {
            if (removed) {
                QDir(QDir(m_paths.screenshotsDir).filePath(QString::number(siteId))).removeRecursively();
                m_sites.refresh();
                m_dashboard.refresh();
                emit siteRemoved(siteId);
            }
        });
}

void AppContext::openExternal(const QString &url) const
{
    const QUrl target = QUrl::fromUserInput(url);
    // Only web and mail links leave the app; anything else from scanned pages is ignored.
    if (target.scheme() == QStringLiteral("http") || target.scheme() == QStringLiteral("https") ||
        target.scheme() == QStringLiteral("mailto")) {
        QDesktopServices::openUrl(target);
    }
}

void AppContext::copyText(const QString &text) const
{
    QGuiApplication::clipboard()->setText(text);
}

void AppContext::openDataFolder() const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_paths.dataDir));
}

void AppContext::openJobLog(qint64 jobId) const
{
    const QString log = QDir(m_paths.jobDir(jobId)).filePath(QStringLiteral("job.log"));
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo::exists(log) ? log : m_paths.jobDir(jobId)));
}

void AppContext::restart()
{
    m_runner.cancelAll();
    QProcess::startDetached(QCoreApplication::applicationFilePath(),
                            QCoreApplication::arguments().mid(1));
    QCoreApplication::quit();
}
