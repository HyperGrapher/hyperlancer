#pragma once

#include <functional>
#include <memory>

#include <QList>
#include <QOAuth2AuthorizationCodeFlow>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class Database;
class QNetworkAccessManager;
class QOAuthHttpServerReplyHandler;
class SecretStore;

// Read-only Gmail access (PRD "Gmail integration"). Sign-in uses OAuth 2.0 for desktop apps:
// the system browser, a loopback redirect on 127.0.0.1 and PKCE. The client ID and secret
// come from the client_secret JSON file the user imports once; they and the refresh token
// are kept in the OS credential store. Tokens never leave this process.
class GmailService : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("GmailService is created by the application.")

    Q_PROPERTY(bool clientConfigured READ isClientConfigured NOTIFY stateChanged)
    Q_PROPERTY(QString clientIdHint READ clientIdHint NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY stateChanged)
    Q_PROPERTY(QString email READ email NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)

public:
    static inline const QByteArray kScope =
        QByteArrayLiteral("https://www.googleapis.com/auth/gmail.readonly");

    GmailService(SecretStore &secrets, Database &database, QNetworkAccessManager &network,
                 QObject *parent = nullptr);
    ~GmailService() override;

    [[nodiscard]] bool isClientConfigured() const;
    [[nodiscard]] QString clientIdHint() const;
    [[nodiscard]] bool isConnected() const { return m_connected; }
    [[nodiscard]] bool isBusy() const { return m_busy; }
    [[nodiscard]] QString email() const { return m_email; }
    [[nodiscard]] QString status() const { return m_status; }

    // Call when the database is ready: restores the session from the stored refresh token.
    void start();

    // Reads Google's client_secret_*.json (Desktop app type). Accepts a path or file URL.
    Q_INVOKABLE void importClientFile(const QString &pathOrUrl);
    Q_INVOKABLE void removeClient();
    // Opens the browser for consent.
    Q_INVOKABLE void connectAccount();
    // Revokes the token at Google and forgets it locally.
    Q_INVOKABLE void disconnectAccount();

    // Searches the mailbox for the site's domain, stores message metadata and emits
    // siteMessagesLoaded. Without a connection it only emits what is already stored.
    Q_INVOKABLE void syncSite(qint64 siteId);
    // Fetches the readable body of one message. Bodies are never stored (PRD): each call
    // asks Gmail and emits messageBodyLoaded.
    Q_INVOKABLE void loadMessageBody(const QString &gmailId);
    // Opens the thread in Gmail, in the configured Chrome profile when there is one.
    Q_INVOKABLE void openThread(const QString &threadId) const;
    void setChromeProfile(const QString &profileName) { m_chromeProfile = profileName; }

    // A valid access token, refreshed when needed; empty string when not connected.
    void withAccessToken(std::function<void(QString)> use);

signals:
    void stateChanged();
    void siteMessagesLoaded(qint64 siteId, const QVariantList &messages, const QString &error);
    void messageBodyLoaded(const QString &gmailId, const QString &body, const QString &error);
    void notice(const QString &message, bool isError);

private:
    void configureFlow();
    void setState(bool connected, bool busy, const QString &status);
    void onGranted();
    void onFailed(const QString &message);
    void loadProfile();
    void fetchMessages(qint64 siteId, const QString &query, const QString &token);
    void emitStoredMessages(qint64 siteId, const QString &error);
    void flushTokenWaiters(const QString &token);

    SecretStore &m_secrets;
    Database &m_database;
    QNetworkAccessManager &m_network;
    QOAuth2AuthorizationCodeFlow m_flow;
    std::unique_ptr<QOAuthHttpServerReplyHandler> m_replyHandler;
    QList<std::function<void(QString)>> m_tokenWaiters;
    bool m_refreshing = false;
    bool m_connected = false;
    bool m_busy = false;
    qint64 m_accountId = 0;
    QString m_email;
    QString m_status;
    QString m_chromeProfile;
};
