#include "GmailInbox.h"

#include <algorithm>
#include <memory>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSet>
#include <QUrlQuery>

#include "Database.h"
#include "DbTime.h"
#include "GmailService.h"
#include "MailStore.h"
#include "SiteOrganizer.h"
#include "SqlExec.h"

namespace {

QUrl endpoint(const QString &path, const QUrlQuery &query = {})
{
    QUrl url(GmailService::kApiBase + path);
    url.setQuery(query);
    return url;
}

QUrlQuery metadataQuery()
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("metadata"));
    for (const char *header : {"From", "To", "Cc", "Subject", "Auto-Submitted"}) {
        query.addQueryItem(QStringLiteral("metadataHeaders"), QString::fromLatin1(header));
    }
    return query;
}

// Stores mail from tracked sites' domains and lets it move their status. True when
// anything changed.
bool storeSiteMail(QSqlDatabase &db, qint64 accountId,
                   const QList<GmailQuery::MessageMeta> &messages)
{
    bool changed = false;
    for (const GmailQuery::MessageMeta &meta : messages) {
        const qint64 siteId = MailStore::siteIdForAddress(db, GmailQuery::addressOf(meta.from));
        if (siteId == 0) {
            continue;
        }
        MailStore::storeMessages(db, accountId, siteId, {meta});
        SiteOrganizer::applyMailStatus(db, siteId);
        changed = true;
    }
    return changed;
}

} // namespace

GmailInbox::GmailInbox(GmailService &gmail, Database &database, QNetworkAccessManager &network,
                       QObject *parent)
    : QObject(parent)
    , m_gmail(gmail)
    , m_database(database)
    , m_network(network)
{
    m_pollTimer.setInterval(kPollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &GmailInbox::poll);
    connect(&m_gmail, &GmailService::accountReady, this,
            [this](qint64, const QString &historyId) { onAccountReady(historyId); });
    connect(&m_gmail, &GmailService::stateChanged, this, &GmailInbox::onStateChanged);
}

int GmailInbox::unreadCount() const
{
    return static_cast<int>(std::count_if(m_messages.begin(), m_messages.end(), [](const QVariant &message) {
        return message.toMap().value(QStringLiteral("unread")).toBool();
    }));
}

void GmailInbox::onAccountReady(const QString &historyId)
{
    m_historyId = historyId;
    m_pollTimer.start();
    refresh();
    poll(); // catches up with mail that arrived while the app was closed
}

void GmailInbox::onStateChanged()
{
    if (m_gmail.isConnected()) {
        return;
    }
    m_pollTimer.stop();
    m_historyId.clear();
    m_messages.clear();
    markSeen();
    m_error.clear();
    m_loading = false;
    emit changed();
}

void GmailInbox::setError(const QString &error)
{
    m_error = error;
    m_loading = false;
    emit changed();
}

void GmailInbox::markSeen()
{
    if (m_newCount == 0) {
        return;
    }
    m_newCount = 0;
    emit newCountChanged();
}

void GmailInbox::refresh()
{
    if (!m_gmail.isConnected() || m_loading) {
        return;
    }
    m_loading = true;
    m_error.clear();
    emit changed();
    QPointer<GmailInbox> self(this);
    m_gmail.withAccessToken([self](const QString &token) {
        if (!self) {
            return;
        }
        if (token.isEmpty()) {
            self->setError(tr("Gmail sign-in expired. Reconnect in Settings."));
            return;
        }
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("labelIds"), QStringLiteral("INBOX"));
        query.addQueryItem(QStringLiteral("maxResults"), QString::number(kInboxSize));
        QNetworkReply *reply = self->m_network.get(
            GmailService::apiRequest(endpoint(QStringLiteral("messages"), query), token));
        connect(reply, &QNetworkReply::finished, self, [self, reply, token]() {
            reply->deleteLater();
            if (!self) {
                return;
            }
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status != 200) {
                self->setError(tr("Could not load the inbox (HTTP %1).").arg(status));
                return;
            }
            QStringList ids;
            for (const QJsonValue &value : QJsonDocument::fromJson(reply->readAll())
                                               .object()
                                               .value(QStringLiteral("messages"))
                                               .toArray()) {
                ids.append(value.toObject().value(QStringLiteral("id")).toString());
            }
            self->fetchMetadata(ids, token, [self](QList<GmailQuery::MessageMeta> metas) {
                std::sort(metas.begin(), metas.end(),
                          [](const auto &a, const auto &b) { return a.sentAt > b.sentAt; });
                QVariantList list;
                for (const GmailQuery::MessageMeta &meta : metas) {
                    list.append(GmailQuery::toVariantMap(meta));
                }
                self->m_messages = list;
                self->m_loading = false;
                emit self->changed();
            });
        });
    });
}

void GmailInbox::fetchMetadata(const QStringList &ids, const QString &token, MetaCallback done)
{
    if (ids.isEmpty()) {
        done({});
        return;
    }
    struct Batch
    {
        int pending = 0;
        QList<GmailQuery::MessageMeta> messages;
        MetaCallback done;
    };
    auto batch = std::make_shared<Batch>();
    batch->pending = static_cast<int>(ids.size());
    batch->done = std::move(done);
    QPointer<GmailInbox> self(this);
    for (const QString &id : ids) {
        QNetworkReply *reply = m_network.get(
            GmailService::apiRequest(endpoint(QStringLiteral("messages/") + id, metadataQuery()), token));
        connect(reply, &QNetworkReply::finished, this, [self, reply, batch]() {
            reply->deleteLater();
            if (self && reply->error() == QNetworkReply::NoError) {
                const GmailQuery::MessageMeta meta = GmailQuery::parseMetadata(
                    QJsonDocument::fromJson(reply->readAll()).object(), self->m_gmail.email());
                if (meta.error.isEmpty()) {
                    batch->messages.append(meta);
                }
            }
            if (--batch->pending == 0 && self) {
                batch->done(batch->messages);
            }
        });
    }
}

void GmailInbox::savePosition(const QString &historyId)
{
    m_historyId = historyId;
    const qint64 accountId = m_gmail.accountId();
    m_database.run(
        this,
        [accountId, historyId](QSqlDatabase &db) {
            QString error;
            SqlExec::run(db, QStringLiteral("UPDATE mail_accounts SET history_id = ? WHERE id = ?"),
                         {historyId, accountId}, &error);
            return error.isEmpty();
        },
        [](bool) {});
}

void GmailInbox::poll()
{
    if (m_polling || !m_gmail.isConnected() || m_historyId.isEmpty()) {
        return;
    }
    m_polling = true;
    QPointer<GmailInbox> self(this);
    m_gmail.withAccessToken([self](const QString &token) {
        if (!self) {
            return;
        }
        if (token.isEmpty()) {
            self->m_polling = false;
            return;
        }
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("startHistoryId"), self->m_historyId);
        query.addQueryItem(QStringLiteral("historyTypes"), QStringLiteral("messageAdded"));
        query.addQueryItem(QStringLiteral("labelId"), QStringLiteral("INBOX"));
        QNetworkReply *reply = self->m_network.get(
            GmailService::apiRequest(endpoint(QStringLiteral("history"), query), token));
        connect(reply, &QNetworkReply::finished, self, [self, reply, token]() {
            reply->deleteLater();
            if (!self) {
                return;
            }
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 404) {
                // Gmail dropped that position (too old): start again from now.
                self->resetHistoryPosition();
                return;
            }
            if (status != 200) {
                self->m_polling = false; // try again next minute
                return;
            }
            const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
            QStringList ids;
            for (const QJsonValue &entry : json.value(QStringLiteral("history")).toArray()) {
                for (const QJsonValue &added : entry.toObject().value(QStringLiteral("messagesAdded")).toArray()) {
                    const QJsonObject message = added.toObject().value(QStringLiteral("message")).toObject();
                    if (message.value(QStringLiteral("labelIds")).toArray().contains(QStringLiteral("INBOX")) &&
                        !ids.contains(message.value(QStringLiteral("id")).toString())) {
                        ids.append(message.value(QStringLiteral("id")).toString());
                    }
                }
            }
            const QString newHistoryId = json.value(QStringLiteral("historyId")).toString();
            if (ids.size() > kMaxNewPerPoll) {
                ids = ids.mid(0, kMaxNewPerPoll);
            }
            self->fetchMetadata(ids, token, [self, newHistoryId](QList<GmailQuery::MessageMeta> metas) {
                self->m_polling = false;
                if (!newHistoryId.isEmpty() && newHistoryId != self->m_historyId) {
                    self->savePosition(newHistoryId);
                }
                self->handleNewMessages(metas);
            });
        });
    });
}

void GmailInbox::resetHistoryPosition()
{
    QPointer<GmailInbox> self(this);
    m_gmail.withAccessToken([self](const QString &token) {
        if (!self) {
            return;
        }
        if (token.isEmpty()) {
            self->m_polling = false;
            return;
        }
        QNetworkReply *reply =
            self->m_network.get(GmailService::apiRequest(endpoint(QStringLiteral("profile")), token));
        connect(reply, &QNetworkReply::finished, self, [self, reply]() {
            reply->deleteLater();
            if (!self) {
                return;
            }
            self->m_polling = false;
            const QString historyId =
                QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("historyId")).toString();
            if (!historyId.isEmpty()) {
                self->savePosition(historyId);
                self->refresh();
            }
        });
    });
}

void GmailInbox::handleNewMessages(const QList<GmailQuery::MessageMeta> &incoming)
{
    QList<GmailQuery::MessageMeta> messages;
    QSet<QString> known;
    for (const QVariant &existing : std::as_const(m_messages)) {
        known.insert(existing.toMap().value(QStringLiteral("gmailId")).toString());
    }
    for (const GmailQuery::MessageMeta &meta : incoming) {
        if (!meta.sent && meta.inInbox && !known.contains(meta.gmailId)) {
            messages.append(meta);
        }
    }
    if (messages.isEmpty()) {
        return;
    }
    std::sort(messages.begin(), messages.end(), [](const auto &a, const auto &b) { return a.sentAt > b.sentAt; });
    QVariantList added;
    for (const GmailQuery::MessageMeta &meta : messages) {
        added.append(GmailQuery::toVariantMap(meta));
    }
    m_messages = added + m_messages;
    m_newCount += static_cast<int>(added.size());
    emit changed();
    emit newMessages(added); // before the count: a watcher that clears the count also clears the notice
    emit newCountChanged();

    const qint64 accountId = m_gmail.accountId();
    m_database.run(
        this, [accountId, messages](QSqlDatabase &db) { return storeSiteMail(db, accountId, messages); },
        [this](bool changed) {
            if (changed) {
                emit siteMailChanged();
            }
        });
}
