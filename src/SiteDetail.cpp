#include "SiteDetail.h"

#include <QPointer>

#include "Database.h"
#include "GmailService.h"
#include "Integrations.h"
#include "JobRunner.h"
#include "PageSpeedClient.h"
#include "SiteStore.h"

SiteDetail::SiteDetail(Database &database, JobRunner &runner, PageSpeedClient &pageSpeed,
                       Integrations &integrations, GmailService &gmail, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_runner(runner)
    , m_pageSpeed(pageSpeed)
    , m_integrations(integrations)
    , m_gmail(gmail)
{
    connect(&m_runner, &JobRunner::jobProgress, this,
            [this](qint64, qint64 siteId, int current, int total, const QString &label) {
                if (siteId == m_siteId) {
                    setProgress(true, total > 0 ? static_cast<double>(current) / total : 0.0, label,
                                current);
                }
            });
    connect(&m_runner, &JobRunner::jobFinished, this,
            [this](qint64, qint64 siteId, const QString &) {
                if (siteId == m_siteId) {
                    setProgress(false, 1.0, QString(), 0);
                    reload();
                }
            });
    connect(&m_gmail, &GmailService::siteMessagesLoaded, this,
            [this](qint64 siteId, const QVariantList &messages, const QString &error) {
                if (siteId != m_siteId) {
                    return;
                }
                m_emails = messages;
                m_emailsError = error;
                m_emailsLoading = false;
                emit emailsChanged();
            });
}

void SiteDetail::setSiteId(qint64 siteId)
{
    if (m_siteId == siteId) {
        return;
    }
    m_siteId = siteId;
    m_site.clear();
    m_snapshot.clear();
    m_snapshots.clear();
    m_activity.clear();
    m_speed.clear();
    m_emails.clear();
    m_emailsError.clear();
    setProgress(false, 0.0, QString(), 0);
    emit siteIdChanged();
    emit emailsChanged();
    reload();
}

void SiteDetail::reload()
{
    if (m_siteId <= 0) {
        return;
    }
    const qint64 siteId = m_siteId;
    const int generation = ++m_generation;
    m_loading = true;
    emit loadedChanged();
    m_database.run(
        this, [siteId](QSqlDatabase &db) { return SiteStore::loadDetail(db, siteId); },
        [this, generation](const QVariantMap &detail) {
            if (generation != m_generation) {
                return;
            }
            m_loading = false;
            m_error = detail.value(QStringLiteral("error")).toString();
            m_site = detail.value(QStringLiteral("site")).toMap();
            m_snapshot = detail.value(QStringLiteral("snapshot")).toMap();
            m_snapshots = detail.value(QStringLiteral("snapshots")).toList();
            m_activity = detail.value(QStringLiteral("activity")).toList();
            m_speed = detail.value(QStringLiteral("speed")).toMap();
            emit loadedChanged();
        });
}

void SiteDetail::analyze()
{
    if (m_siteId <= 0 || m_site.isEmpty()) {
        return;
    }
    setProgress(true, 0.0, tr("queued"), 0);
    m_runner.enqueueProfile(m_siteId, m_site.value(QStringLiteral("url")).toString(),
                            [this](qint64, const QString &error) {
                                if (!error.isEmpty()) {
                                    setProgress(false, 0.0, QString(), 0);
                                    emit notice(error, true);
                                }
                            });
}

void SiteDetail::checkSpeed()
{
    if (m_siteId <= 0 || m_speedRunning) {
        return;
    }
    const qint64 siteId = m_siteId;
    QString url = m_site.value(QStringLiteral("final_url")).toString();
    if (url.isEmpty()) {
        url = m_site.value(QStringLiteral("url")).toString();
    }
    m_speedRunning = true;
    emit speedRunningChanged();
    QPointer<SiteDetail> self(this);
    const QString apiKey = m_integrations.pageSpeedKey();
    // Without a key, the Gmail sign-in token of the same Google Cloud project is used.
    const auto runWithToken = [self, siteId, url, apiKey](const QString &token) {
        if (!self) {
            return;
        }
        self->m_pageSpeed.run(url, apiKey, token, [self, siteId](const PageSpeed::Result &result) {
            if (!self) {
                return;
            }
            self->m_database.run(
                self,
                [siteId, result](QSqlDatabase &db) {
                    SiteStore::saveSpeedCheck(db, siteId, result.score, result.metrics,
                                              result.error);
                    SiteStore::addApiUsage(db, QStringLiteral("pagespeed"), 1);
                    return true;
                },
                [self, siteId, error = result.error](bool) {
                    if (!self) {
                        return;
                    }
                    self->m_speedRunning = false;
                    emit self->speedRunningChanged();
                    if (!error.isEmpty()) {
                        emit self->notice(error, true);
                    }
                    if (siteId == self->m_siteId) {
                        self->reload();
                    }
                });
        });
    };
    if (apiKey.isEmpty()) {
        m_gmail.withAccessToken(runWithToken);
    } else {
        runWithToken(QString());
    }
}

void SiteDetail::loadEmails()
{
    if (m_siteId <= 0) {
        return;
    }
    m_emailsLoading = true;
    emit emailsChanged();
    m_gmail.syncSite(m_siteId);
}

void SiteDetail::showSnapshot(qint64 snapshotId)
{
    m_database.run(
        this, [snapshotId](QSqlDatabase &db) { return SiteStore::loadSnapshot(db, snapshotId); },
        [this](const QVariantMap &snapshot) {
            if (!snapshot.isEmpty()) {
                m_snapshot = snapshot;
                emit loadedChanged();
            }
        });
}

void SiteDetail::setProgress(bool analyzing, double progress, const QString &label, int step)
{
    m_analyzing = analyzing;
    m_progress = progress;
    m_progressLabel = label;
    m_progressStep = step;
    emit progressChanged();
}
