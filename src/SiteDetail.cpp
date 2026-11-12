#include "SiteDetail.h"

#include <memory>

#include <QDir>
#include <QPointer>
#include <QUrl>

#include "Database.h"
#include "GmailService.h"
#include "Integrations.h"
#include "JobRunner.h"
#include "PageSpeedClient.h"
#include "SiteOrganizer.h"
#include "SiteStore.h"

SiteDetail::SiteDetail(Database &database, JobRunner &runner, PageSpeedClient &pageSpeed,
                       Integrations &integrations, GmailService &gmail, QString dataDir,
                       QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_runner(runner)
    , m_pageSpeed(pageSpeed)
    , m_integrations(integrations)
    , m_gmail(gmail)
    , m_dataDir(std::move(dataDir))
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
                reload(); // mail can move the status to contacted or replied
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
    m_speedDesktop.clear();
    m_screenshots.clear();
    m_organizer.clear();
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
        this,
        [siteId](QSqlDatabase &db) {
            QVariantMap detail = SiteStore::loadDetail(db, siteId);
            detail.insert(QStringLiteral("organizer"), SiteOrganizer::load(db, siteId));
            return detail;
        },
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
            m_speedDesktop = detail.value(QStringLiteral("speedDesktop")).toMap();
            m_screenshots.clear();
            const QVariantMap stored = detail.value(QStringLiteral("screenshots")).toMap();
            for (auto it = stored.begin(); it != stored.end(); ++it) {
                const QVariantMap shot = it.value().toMap();
                m_screenshots.insert(
                    it.key(),
                    QVariantMap{{QStringLiteral("url"),
                                 QUrl::fromLocalFile(QDir(m_dataDir).filePath(
                                                         shot.value(QStringLiteral("path")).toString()))},
                                {QStringLiteral("createdAt"), shot.value(QStringLiteral("createdAt"))}});
            }
            m_organizer = detail.value(QStringLiteral("organizer")).toMap();
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

    // Both strategies run at once; the check is over when both have answered.
    struct Pending
    {
        int remaining = 2;
        QString firstError;
    };
    auto pending = std::make_shared<Pending>();
    QPointer<SiteDetail> self(this);
    for (const QString &strategy : {QStringLiteral("mobile"), QStringLiteral("desktop")}) {
        m_pageSpeed.run(
            url, m_integrations.pageSpeedKey(), strategy,
            [self, siteId, strategy, pending](const PageSpeed::Result &result) {
                if (!self) {
                    return;
                }
                self->m_database.run(
                    self,
                    [siteId, strategy, result, dataDir = self->m_dataDir](QSqlDatabase &db) {
                        SiteStore::saveSpeedCheck(db, siteId, strategy, result.score, result.metrics,
                                                  result.error);
                        SiteStore::addApiUsage(db, QStringLiteral("pagespeed"), 1);
                        if (!result.screenshot.isEmpty()) {
                            SiteStore::saveScreenshot(db, dataDir, siteId, strategy, result.screenshot);
                        }
                        return true;
                    },
                    [self, siteId, pending, error = result.error](bool) {
                        if (!self) {
                            return;
                        }
                        if (pending->firstError.isEmpty()) {
                            pending->firstError = error;
                        }
                        if (--pending->remaining > 0) {
                            return;
                        }
                        self->m_speedRunning = false;
                        emit self->speedRunningChanged();
                        emit self->speedCheckFinished(pending->firstError.isEmpty());
                        if (!pending->firstError.isEmpty()) {
                            emit self->notice(pending->firstError, true);
                        }
                        if (siteId == self->m_siteId) {
                            self->reload();
                        }
                    });
            });
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

QStringList SiteDetail::statuses() const
{
    return SiteOrganizer::statuses();
}

void SiteDetail::saveOrganizer(std::function<bool(QSqlDatabase &)> change)
{
    if (m_siteId <= 0) {
        return;
    }
    m_database.run(
        this, [change = std::move(change)](QSqlDatabase &db) { return change(db); },
        [this](bool saved) {
            if (!saved) {
                emit notice(tr("Could not save the change."), true);
            }
            emit organizerChanged();
            reload();
        });
}

void SiteDetail::setStatus(const QString &status)
{
    saveOrganizer([siteId = m_siteId, status](QSqlDatabase &db) {
        return SiteOrganizer::setStatus(db, siteId, status);
    });
}

void SiteDetail::setCategory(const QString &name)
{
    saveOrganizer([siteId = m_siteId, name](QSqlDatabase &db) {
        return SiteOrganizer::setCategory(db, siteId, name);
    });
}

void SiteDetail::addTag(const QString &name)
{
    saveOrganizer([siteId = m_siteId, name](QSqlDatabase &db) {
        return SiteOrganizer::addTag(db, siteId, name);
    });
}

void SiteDetail::removeTag(qint64 tagId)
{
    saveOrganizer([siteId = m_siteId, tagId](QSqlDatabase &db) {
        return SiteOrganizer::removeTag(db, siteId, tagId);
    });
}

void SiteDetail::setDeadline(const QString &dueOn)
{
    saveOrganizer([siteId = m_siteId, dueOn](QSqlDatabase &db) {
        return SiteOrganizer::setDeadline(db, siteId, dueOn);
    });
}

void SiteDetail::addNote(const QString &title, const QString &body)
{
    saveOrganizer([siteId = m_siteId, title, body](QSqlDatabase &db) {
        return SiteOrganizer::addNote(db, siteId, title, body);
    });
}

void SiteDetail::deleteNote(qint64 noteId)
{
    saveOrganizer([noteId](QSqlDatabase &db) { return SiteOrganizer::deleteNote(db, noteId); });
}

void SiteDetail::addTodo(const QString &title, const QString &dueOn)
{
    saveOrganizer([siteId = m_siteId, title, dueOn](QSqlDatabase &db) {
        return SiteOrganizer::addTodo(db, siteId, title, dueOn);
    });
}

void SiteDetail::setTodoDone(qint64 todoId, bool done)
{
    saveOrganizer([todoId, done](QSqlDatabase &db) {
        return SiteOrganizer::setTodoDone(db, todoId, done);
    });
}

void SiteDetail::deleteTodo(qint64 todoId)
{
    saveOrganizer([todoId](QSqlDatabase &db) { return SiteOrganizer::deleteTodo(db, todoId); });
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
