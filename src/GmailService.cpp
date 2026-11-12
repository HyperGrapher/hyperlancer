#include "GmailService.h"

#include <memory>

#include <QDesktopServices>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QOAuthHttpServerReplyHandler>
#include <QPointer>
#include <QSqlQuery>
#include <QUrlQuery>

#include "ChromeProfile.h"
#include "Database.h"
#include "DbTime.h"
#include "GmailQuery.h"
#include "MailStore.h"
#include "SecretStore.h"
#include "SiteOrganizer.h"
#include "SiteStore.h"
#include "SqlExec.h"

namespace {

const QUrl kAuthorizationUrl(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth"));
const QUrl kTokenUrl(QStringLiteral("https://oauth2.googleapis.com/token"));
const QUrl kRevokeUrl(QStringLiteral("https://oauth2.googleapis.com/revoke"));
constexpr int kMaxMessagesPerSync = 40;

QString localPath(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl.trimmed();
}

// --- storage (database thread) -------------------------------------------------------------

struct Account
{
    qint64 id = 0;
    QString historyId; // where background polling resumes
};

// The mailbox row. The stored history position is kept, so mail that arrived while the app
// was closed is still seen; only a new row starts from Gmail's current position.
Account upsertAccount(QSqlDatabase &db, const QString &email, const QString &historyId)
{
    QSqlQuery existing = SqlExec::run(
        db, QStringLiteral("SELECT id, history_id FROM mail_accounts WHERE email = ?"), {email});
    if (existing.next()) {
        const qint64 id = existing.value(0).toLongLong();
        const QString stored = existing.value(1).toString();
        SqlExec::run(db,
                     QStringLiteral("UPDATE mail_accounts SET status = 'connected', history_id = "
                                    "COALESCE(history_id, ?) WHERE id = ?"),
                     {SqlExec::textOrNull(historyId), id});
        return {id, stored.isEmpty() ? historyId : stored};
    }
    QSqlQuery insert = SqlExec::run(
        db,
        QStringLiteral(
            "INSERT INTO mail_accounts (email, history_id, status, created_at) VALUES (?, "
            "?, 'connected', ?)"),
        {email, SqlExec::textOrNull(historyId), DbTime::now()});
    return {insert.lastInsertId().toLongLong(), historyId};
}

QVariantList loadSiteMessages(QSqlDatabase &db, qint64 siteId)
{
    QVariantList messages;
    QSqlQuery query = SqlExec::run(
        db,
        QStringLiteral(
            "SELECT gmail_id, thread_id, direction, from_addr, to_addrs, subject, snippet, "
            "sent_at, has_attachments, is_bounce, is_auto_reply FROM mail_messages WHERE "
            "site_id = ? ORDER BY sent_at DESC LIMIT 200"),
        {siteId});
    while (query.next()) {
        messages.append(QVariantMap{
            {QStringLiteral("gmailId"), query.value(0)},
            {QStringLiteral("threadId"), query.value(1)},
            {QStringLiteral("direction"), query.value(2)},
            {QStringLiteral("from"), query.value(3)},
            {QStringLiteral("to"), query.value(4)},
            {QStringLiteral("subject"), query.value(5)},
            {QStringLiteral("snippet"), query.value(6)},
            {QStringLiteral("sentAt"), DbTime::toIso(query.value(7))},
            {QStringLiteral("hasAttachments"), query.value(8).toBool()},
            {QStringLiteral("isBounce"), query.value(9).toBool()},
            {QStringLiteral("isAutoReply"), query.value(10).toBool()},
        });
    }
    return messages;
}

} // namespace

QNetworkRequest GmailService::apiRequest(const QUrl &url, const QString &token)
{
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    request.setTransferTimeout(30000);
    return request;
}

GmailService::GmailService(SecretStore &secrets, Database &database, QNetworkAccessManager &network,
                           QObject *parent)
    : QObject(parent)
    , m_secrets(secrets)
    , m_database(database)
    , m_network(network)
{
    m_flow.setNetworkAccessManager(&m_network);
    m_flow.setAuthorizationUrl(kAuthorizationUrl);
    m_flow.setTokenUrl(kTokenUrl);
    m_flow.setRequestedScopeTokens({kScope});
    m_flow.setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
    // Google returns a refresh token only for offline access, and only on a fresh consent.
    m_flow.setModifyParametersFunction(
        [](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant> *parameters) {
            if (stage == QAbstractOAuth::Stage::RequestingAuthorization) {
                parameters->insert(QStringLiteral("access_type"), QStringLiteral("offline"));
                parameters->insert(QStringLiteral("prompt"), QStringLiteral("consent"));
            }
        });
    connect(&m_flow, &QAbstractOAuth::authorizeWithBrowser, this,
            [](const QUrl &url) { QDesktopServices::openUrl(url); });
    connect(&m_flow, &QAbstractOAuth::granted, this, &GmailService::onGranted);
    connect(&m_flow, &QAbstractOAuth::requestFailed, this, [this](QAbstractOAuth::Error error) {
        onFailed(tr("Google sign-in failed (error %1).").arg(static_cast<int>(error)));
    });
    connect(&m_flow, &QAbstractOAuth2::serverReportedErrorOccurred, this,
            [this](const QString &error, const QString &description, const QUrl &) {
                onFailed(tr("Google refused the sign-in: %1 %2").arg(error, description));
            });
    configureFlow();
    m_status =
        isClientConfigured() ? tr("Not connected") : tr("Import the OAuth client file first");
}

GmailService::~GmailService() = default;

bool GmailService::isClientConfigured() const
{
    return m_secrets.read(SecretKeys::kGmailClientId).has_value() &&
           m_secrets.read(SecretKeys::kGmailClientSecret).has_value();
}

QString GmailService::clientIdHint() const
{
    const QString id = m_secrets.read(SecretKeys::kGmailClientId).value_or(QString());
    return id.isEmpty() ? QString() : id.left(12) + QStringLiteral("…");
}

void GmailService::configureFlow()
{
    m_flow.setClientIdentifier(m_secrets.read(SecretKeys::kGmailClientId).value_or(QString()));
    m_flow.setClientIdentifierSharedKey(
        m_secrets.read(SecretKeys::kGmailClientSecret).value_or(QString()));
}

void GmailService::setState(bool connected, bool busy, const QString &status)
{
    m_connected = connected;
    m_busy = busy;
    m_status = status;
    emit stateChanged();
}

void GmailService::start()
{
    const std::optional<QString> refreshToken = m_secrets.read(SecretKeys::kGmailRefreshToken);
    if (!isClientConfigured() || !refreshToken || refreshToken->isEmpty()) {
        return;
    }
    m_flow.setRefreshToken(*refreshToken);
    setState(false, true, tr("Reconnecting…"));
    m_refreshing = true;
    m_flow.refreshTokens();
}

void GmailService::importClientFile(const QString &pathOrUrl)
{
    QFile file(localPath(pathOrUrl));
    if (!file.open(QIODevice::ReadOnly)) {
        emit notice(tr("Could not open %1.").arg(file.fileName()), true);
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.read(64 * 1024)).object();
    QJsonObject client = root.value(QStringLiteral("installed")).toObject();
    if (client.isEmpty()) {
        if (!root.value(QStringLiteral("web")).toObject().isEmpty()) {
            emit notice(tr("This is a \"Web application\" client. Create an OAuth client of type "
                           "\"Desktop app\" in Google Cloud Console and import that file."),
                        true);
        } else {
            emit notice(tr("This file is not a Google OAuth client file."), true);
        }
        return;
    }
    const QString clientId = client.value(QStringLiteral("client_id")).toString();
    const QString clientSecret = client.value(QStringLiteral("client_secret")).toString();
    if (clientId.isEmpty() || clientSecret.isEmpty()) {
        emit notice(tr("The client file has no client_id or client_secret."), true);
        return;
    }
    if (!m_secrets.write(SecretKeys::kGmailClientId, clientId) ||
        !m_secrets.write(SecretKeys::kGmailClientSecret, clientSecret)) {
        emit notice(tr("Could not save the client in %1.").arg(SecretStore::backendName()), true);
        return;
    }
    configureFlow();
    setState(m_connected, false,
             m_connected ? m_status : tr("Client imported. Connect your mailbox."));
    emit notice(tr("OAuth client saved to %1. You can delete or move the JSON file now.")
                    .arg(SecretStore::backendName()),
                false);
}

void GmailService::removeClient()
{
    if (m_connected) {
        disconnectAccount();
    }
    m_secrets.remove(SecretKeys::kGmailClientId);
    m_secrets.remove(SecretKeys::kGmailClientSecret);
    configureFlow();
    setState(false, false, tr("Import the OAuth client file first"));
}

void GmailService::connectAccount()
{
    if (!isClientConfigured()) {
        emit notice(tr("Import the OAuth client file first."), true);
        return;
    }
    // A fresh loopback listener per attempt, on a free port of 127.0.0.1.
    m_replyHandler = std::make_unique<QOAuthHttpServerReplyHandler>(QHostAddress::LocalHost, 0);
    if (!m_replyHandler->isListening()) {
        m_replyHandler.reset();
        emit notice(tr("Could not open a local port for the Google sign-in."), true);
        return;
    }
    m_replyHandler->setCallbackText(
        tr("Hyperlancer is now connected to Gmail. You can close this tab."));
    m_flow.setReplyHandler(m_replyHandler.get());
    setState(false, true, tr("Waiting for you to approve in the browser…"));
    m_flow.grant();
}

void GmailService::disconnectAccount()
{
    const QString token =
        m_secrets.read(SecretKeys::kGmailRefreshToken).value_or(m_flow.refreshToken());
    if (!token.isEmpty()) {
        QNetworkRequest request(kRevokeUrl);
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/x-www-form-urlencoded"));
        QUrlQuery body;
        body.addQueryItem(QStringLiteral("token"), token);
        QNetworkReply *reply = m_network.post(request, body.toString(QUrl::FullyEncoded).toUtf8());
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
    m_secrets.remove(SecretKeys::kGmailRefreshToken);
    m_flow.setRefreshToken(QString());
    m_flow.setToken(QString());
    const QString email = m_email;
    m_database.post([email](QSqlDatabase &db) {
        SqlExec::run(
            db, QStringLiteral("UPDATE mail_accounts SET status = 'disconnected' WHERE email = ?"),
            {email});
    });
    m_email.clear();
    m_accountId = 0;
    flushTokenWaiters(QString());
    setState(false, false, tr("Disconnected"));
}

void GmailService::onGranted()
{
    m_refreshing = false;
    if (m_replyHandler) {
        m_replyHandler->close();
    }
    const QString refreshToken = m_flow.refreshToken();
    if (!refreshToken.isEmpty() && !m_secrets.write(SecretKeys::kGmailRefreshToken, refreshToken)) {
        emit notice(
            tr("Could not save the Gmail sign-in in %1; you will need to connect again next time.")
                .arg(SecretStore::backendName()),
            true);
    }
    if (!m_flow.grantedScopeTokens().contains(kScope)) {
        onFailed(tr("Gmail read access was not granted. Tick the Gmail box on the consent page."));
        return;
    }
    if (m_email.isEmpty()) {
        loadProfile();
    } else {
        setState(true, false, tr("Connected as %1").arg(m_email));
    }
    flushTokenWaiters(m_flow.token());
}

void GmailService::onFailed(const QString &message)
{
    qWarning("Gmail: %s", qPrintable(message));
    m_refreshing = false;
    if (m_replyHandler) {
        m_replyHandler->close();
    }
    flushTokenWaiters(QString());
    setState(false, false, message);
}

void GmailService::loadProfile()
{
    QNetworkReply *reply =
        m_network.get(apiRequest(QUrl(kApiBase + QStringLiteral("profile")), m_flow.token()));
    QPointer<GmailService> self(this);
    connect(reply, &QNetworkReply::finished, this, [self, reply]() {
        reply->deleteLater();
        if (!self) {
            return;
        }
        const QJsonObject profile = QJsonDocument::fromJson(reply->readAll()).object();
        const QString email = profile.value(QStringLiteral("emailAddress")).toString().toLower();
        if (reply->error() != QNetworkReply::NoError || email.isEmpty()) {
            self->onFailed(tr("Could not read the Gmail profile: %1").arg(reply->errorString()));
            return;
        }
        self->m_email = email;
        const QString historyId = profile.value(QStringLiteral("historyId")).toString();
        self->m_database.run(
            self,
            [email, historyId](QSqlDatabase &db) { return upsertAccount(db, email, historyId); },
            [self](const Account &account) {
                if (self) {
                    self->m_accountId = account.id;
                    emit self->accountReady(account.id, account.historyId);
                }
            });
        self->setState(true, false, tr("Connected as %1").arg(email));
    });
}

void GmailService::withAccessToken(std::function<void(QString)> use)
{
    if (m_flow.refreshToken().isEmpty() && m_flow.token().isEmpty()) {
        use(QString());
        return;
    }
    const QDateTime expiry = m_flow.expirationAt();
    if (!m_flow.token().isEmpty() && expiry.isValid() &&
        expiry > QDateTime::currentDateTimeUtc().addSecs(60)) {
        use(m_flow.token());
        return;
    }
    m_tokenWaiters.append(std::move(use));
    if (!m_refreshing) {
        m_refreshing = true;
        m_flow.refreshTokens();
    }
}

void GmailService::flushTokenWaiters(const QString &token)
{
    const QList<std::function<void(QString)>> waiters = std::exchange(m_tokenWaiters, {});
    for (const auto &waiter : waiters) {
        waiter(token);
    }
}

void GmailService::syncSite(qint64 siteId)
{
    if (!m_connected || m_accountId == 0) {
        emitStoredMessages(siteId, m_connected ? QString() : tr("Gmail is not connected."));
        return;
    }
    m_database.run(
        this, [siteId](QSqlDatabase &db) { return SiteStore::siteDomains(db, siteId); },
        [this, siteId](const QStringList &domains) {
            const QString query = GmailQuery::buildSearch(domains, {});
            if (query.isEmpty()) {
                emitStoredMessages(siteId,
                                   tr("This site's domain cannot be matched (free-mail domain)."));
                return;
            }
            withAccessToken([this, siteId, query](const QString &token) {
                if (token.isEmpty()) {
                    emitStoredMessages(siteId, tr("Gmail sign-in expired. Reconnect in Settings."));
                    return;
                }
                fetchMessages(siteId, query, token);
            });
        });
}

void GmailService::fetchMessages(qint64 siteId, const QString &query, const QString &token)
{
    QUrl listUrl(kApiBase + QStringLiteral("messages"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("q"), query);
    params.addQueryItem(QStringLiteral("maxResults"), QString::number(kMaxMessagesPerSync));
    listUrl.setQuery(params);
    QNetworkReply *listReply = m_network.get(apiRequest(listUrl, token));
    QPointer<GmailService> self(this);
    connect(listReply, &QNetworkReply::finished, this, [self, listReply, siteId, token]() {
        listReply->deleteLater();
        if (!self) {
            return;
        }
        const int status = listReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            self->emitStoredMessages(siteId, tr("Gmail search failed (HTTP %1).").arg(status));
            return;
        }
        const QJsonArray ids = QJsonDocument::fromJson(listReply->readAll())
                                   .object()
                                   .value(QStringLiteral("messages"))
                                   .toArray();
        if (ids.isEmpty()) {
            self->emitStoredMessages(siteId, QString());
            return;
        }
        // Collect every metadata reply, then store them in one go.
        struct Batch
        {
            int pending = 0;
            QList<GmailQuery::MessageMeta> messages;
        };
        auto batch = std::make_shared<Batch>();
        batch->pending = static_cast<int>(ids.size());
        const QString accountEmail = self->m_email;
        const qint64 accountId = self->m_accountId;
        for (const QJsonValue &value : ids) {
            QUrl getUrl(kApiBase + QStringLiteral("messages/") +
                        value.toObject().value(QStringLiteral("id")).toString());
            QUrlQuery getParams;
            getParams.addQueryItem(QStringLiteral("format"), QStringLiteral("metadata"));
            for (const char *header : {"From", "To", "Cc", "Subject", "Auto-Submitted"}) {
                getParams.addQueryItem(QStringLiteral("metadataHeaders"),
                                       QString::fromLatin1(header));
            }
            getUrl.setQuery(getParams);
            QNetworkReply *getReply = self->m_network.get(apiRequest(getUrl, token));
            connect(getReply, &QNetworkReply::finished, self,
                    [self, getReply, batch, siteId, accountEmail, accountId]() {
                        getReply->deleteLater();
                        if (getReply->error() == QNetworkReply::NoError) {
                            const GmailQuery::MessageMeta meta = GmailQuery::parseMetadata(
                                QJsonDocument::fromJson(getReply->readAll()).object(),
                                accountEmail);
                            if (meta.error.isEmpty()) {
                                batch->messages.append(meta);
                            }
                        }
                        if (--batch->pending > 0 || !self) {
                            return;
                        }
                        const QList<GmailQuery::MessageMeta> messages = batch->messages;
                        self->m_database.run(
                            self,
                            [accountId, siteId, messages](QSqlDatabase &db) {
                                MailStore::storeMessages(db, accountId, siteId, messages);
                                return SiteOrganizer::applyMailStatus(db, siteId);
                            },
                            [self, siteId](bool statusChanged) {
                                if (self) {
                                    self->emitStoredMessages(siteId, QString());
                                    if (statusChanged) {
                                        emit self->siteMailChanged();
                                    }
                                }
                            });
                    });
        }
    });
}

void GmailService::emitStoredMessages(qint64 siteId, const QString &error)
{
    m_database.run(
        this, [siteId](QSqlDatabase &db) { return loadSiteMessages(db, siteId); },
        [this, siteId, error](const QVariantList &messages) {
            emit siteMessagesLoaded(siteId, messages, error);
        });
}

void GmailService::loadMessageBody(const QString &gmailId)
{
    withAccessToken([this, gmailId](const QString &token) {
        if (token.isEmpty()) {
            emit messageBodyLoaded(gmailId, {}, tr("Gmail is not connected."));
            return;
        }
        QUrl url(kApiBase + QStringLiteral("messages/") + gmailId);
        QUrlQuery params;
        params.addQueryItem(QStringLiteral("format"), QStringLiteral("full"));
        url.setQuery(params);
        QNetworkReply *reply = m_network.get(apiRequest(url, token));
        QPointer<GmailService> self(this);
        connect(reply, &QNetworkReply::finished, this, [self, reply, gmailId]() {
            reply->deleteLater();
            if (!self) {
                return;
            }
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status != 200) {
                emit self->messageBodyLoaded(gmailId, {},
                                             tr("Could not load the message (HTTP %1).").arg(status));
                return;
            }
            const QString body =
                GmailQuery::bodyText(QJsonDocument::fromJson(reply->readAll()).object());
            emit self->messageBodyLoaded(gmailId, body, body.isEmpty() ? tr("This message has no text.") : QString());
        });
    });
}

void GmailService::openThread(const QString &threadId) const
{
    const QUrl url(QStringLiteral("https://mail.google.com/mail/u/0/#all/") + threadId);
    if (!ChromeProfile::open(url, m_chromeProfile)) {
        QDesktopServices::openUrl(url);
    }
}
