#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// Pure helpers for the Gmail integration: building search queries for a site and turning a
// users.messages.get (format=metadata) response into a stored row. No network here.
namespace GmailQuery {

// Free-mail domains never match a business by domain, only by exact address (PRD).
[[nodiscard]] bool isFreeMailDomain(const QString &domain);

// "from:(@a.com OR @b.com) OR to:(...) OR cc:(...)" for the domains, plus exact addresses.
// Free-mail domains are dropped. Empty when nothing is left to search for.
[[nodiscard]] QString buildSearch(const QStringList &domains, const QStringList &addresses);

// The bare address from a header value such as "Ayşe <ayse@firma.com.tr>", lower case.
[[nodiscard]] QString addressOf(const QString &headerValue);
[[nodiscard]] QStringList addressesOf(const QString &headerValue);

struct MessageMeta
{
    QString gmailId;
    QString threadId;
    QString from;
    QString to;
    QString subject;
    QString snippet;
    QDateTime sentAt;
    bool sent = false; // the mailbox owner sent it
    bool hasAttachments = false;
    bool isBounce = false;
    bool isAutoReply = false;
    QString error;
};

// accountEmail decides the direction: a message from the mailbox owner is "sent".
[[nodiscard]] MessageMeta parseMetadata(const QJsonObject &message, const QString &accountEmail);

} // namespace GmailQuery
