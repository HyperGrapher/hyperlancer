#pragma once

#include <functional>

#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "GmailQuery.h"

class Database;
class GmailService;
class QNetworkAccessManager;

// The mailbox's inbox (messages still in INBOX, not archived) and the background check for
// new mail. The check polls users.history.list from the stored historyId once a minute: a
// desktop app has no public HTTPS address for Gmail push, and a poll costs a few quota units.
// Mail from a tracked site's domain is also stored and can move the site's status.
class GmailInbox : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("GmailInbox is created by the application.")

    Q_PROPERTY(QVariantList messages READ messages NOTIFY changed)
    Q_PROPERTY(bool loading READ isLoading NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int unreadCount READ unreadCount NOTIFY changed)
    // Mail that arrived while the app ran and has not been looked at yet (the sidebar badge).
    Q_PROPERTY(int newCount READ newCount NOTIFY newCountChanged)

public:
    static constexpr int kPollIntervalMs = 60'000;
    static constexpr int kInboxSize = 30;
    static constexpr int kMaxNewPerPoll = 25;

    GmailInbox(GmailService &gmail, Database &database, QNetworkAccessManager &network,
               QObject *parent = nullptr);

    [[nodiscard]] QVariantList messages() const { return m_messages; }
    [[nodiscard]] bool isLoading() const { return m_loading; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] int unreadCount() const;
    [[nodiscard]] int newCount() const { return m_newCount; }

    // Reloads the newest inbox messages.
    Q_INVOKABLE void refresh();
    // The user has seen the new mail (the Emails page is open): clears the badge.
    Q_INVOKABLE void markSeen();

signals:
    void changed();
    void newCountChanged();
    // Messages that arrived since the last check, newest first.
    void newMessages(const QVariantList &messages);
    // Stored mail changed a site (messages or status); lists should refresh.
    void siteMailChanged();

private:
    using MetaCallback = std::function<void(QList<GmailQuery::MessageMeta>)>;

    void onAccountReady(const QString &historyId);
    void onStateChanged();
    void poll();
    void resetHistoryPosition();
    void fetchMetadata(const QStringList &ids, const QString &token, MetaCallback done);
    void savePosition(const QString &historyId);
    void handleNewMessages(const QList<GmailQuery::MessageMeta> &messages);
    void setError(const QString &error);

    GmailService &m_gmail;
    Database &m_database;
    QNetworkAccessManager &m_network;
    QTimer m_pollTimer;
    QString m_historyId;
    QVariantList m_messages;
    QString m_error;
    bool m_loading = false;
    bool m_polling = false;
    int m_newCount = 0;
};
