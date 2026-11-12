#include "GmailQuery.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>

namespace GmailQuery {

bool isFreeMailDomain(const QString &domain)
{
    static const QSet<QString> kFreeMail{
        QStringLiteral("gmail.com"),      QStringLiteral("googlemail.com"),
        QStringLiteral("hotmail.com"),    QStringLiteral("outlook.com"),
        QStringLiteral("live.com"),       QStringLiteral("yahoo.com"),
        QStringLiteral("yandex.com"),     QStringLiteral("yandex.ru"),
        QStringLiteral("icloud.com"),     QStringLiteral("me.com"),
        QStringLiteral("aol.com"),        QStringLiteral("proton.me"),
        QStringLiteral("protonmail.com"), QStringLiteral("hotmail.com.tr"),
        QStringLiteral("mynet.com"),
    };
    return kFreeMail.contains(domain.trimmed().toLower());
}

QString buildSearch(const QStringList &domains, const QStringList &addresses)
{
    QStringList targets;
    for (const QString &domain : domains) {
        const QString clean = domain.trimmed().toLower();
        if (!clean.isEmpty() && !isFreeMailDomain(clean)) {
            targets.append(QStringLiteral("@") + clean);
        }
    }
    for (const QString &address : addresses) {
        const QString clean = address.trimmed().toLower();
        if (clean.contains(u'@')) {
            targets.append(clean);
        }
    }
    targets.removeDuplicates();
    if (targets.isEmpty()) {
        return {};
    }
    const QString group =
        QStringLiteral("(") + targets.join(QStringLiteral(" OR ")) + QStringLiteral(")");
    return QStringLiteral("from:%1 OR to:%1 OR cc:%1").arg(group);
}

QString addressOf(const QString &headerValue)
{
    static const QRegularExpression angle(QStringLiteral("<([^>]+)>"));
    const QRegularExpressionMatch match = angle.match(headerValue);
    const QString address = match.hasMatch() ? match.captured(1) : headerValue;
    return address.trimmed().toLower();
}

QStringList addressesOf(const QString &headerValue)
{
    static const QRegularExpression email(
        QStringLiteral("[A-Za-z0-9._%+'-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}"));
    QStringList found;
    auto it = email.globalMatch(headerValue);
    while (it.hasNext()) {
        found.append(it.next().captured(0).toLower());
    }
    found.removeDuplicates();
    return found;
}

MessageMeta parseMetadata(const QJsonObject &message, const QString &accountEmail)
{
    MessageMeta meta;
    meta.gmailId = message.value(QStringLiteral("id")).toString();
    meta.threadId = message.value(QStringLiteral("threadId")).toString();
    if (meta.gmailId.isEmpty()) {
        meta.error = QObject::tr("Message without an id.");
        return meta;
    }
    meta.snippet = message.value(QStringLiteral("snippet")).toString();
    const qint64 internalDate =
        message.value(QStringLiteral("internalDate")).toString().toLongLong();
    if (internalDate > 0) {
        meta.sentAt = QDateTime::fromMSecsSinceEpoch(internalDate, QTimeZone::UTC);
    }
    const QJsonObject payload = message.value(QStringLiteral("payload")).toObject();
    QString autoSubmitted;
    for (const QJsonValue &value : payload.value(QStringLiteral("headers")).toArray()) {
        const QJsonObject header = value.toObject();
        const QString name = header.value(QStringLiteral("name")).toString().toLower();
        const QString content = header.value(QStringLiteral("value")).toString();
        if (name == QStringLiteral("from")) {
            meta.from = content;
        } else if (name == QStringLiteral("to")) {
            meta.to = content;
        } else if (name == QStringLiteral("cc") && !content.isEmpty()) {
            meta.to += (meta.to.isEmpty() ? QString() : QStringLiteral(", ")) + content;
        } else if (name == QStringLiteral("subject")) {
            meta.subject = content;
        } else if (name == QStringLiteral("auto-submitted")) {
            autoSubmitted = content.trimmed().toLower();
        }
    }
    const QString fromAddress = addressOf(meta.from);
    meta.sent = !accountEmail.isEmpty() && fromAddress == accountEmail.trimmed().toLower();
    meta.isBounce = fromAddress.startsWith(QStringLiteral("mailer-daemon@")) ||
                    fromAddress.startsWith(QStringLiteral("postmaster@"));
    meta.isAutoReply = !autoSubmitted.isEmpty() && autoSubmitted != QStringLiteral("no");
    // Gmail flags messages with attachments with parts that have a filename; metadata format
    // does not include parts, so fall back to the label-free mimeType hint.
    meta.hasAttachments =
        payload.value(QStringLiteral("mimeType")).toString() == QStringLiteral("multipart/mixed");
    return meta;
}

namespace {

QString decodePart(const QJsonObject &part)
{
    const QByteArray data = part.value(QStringLiteral("body")).toObject().value(QStringLiteral("data")).toString().toLatin1();
    return QString::fromUtf8(QByteArray::fromBase64(data, QByteArray::Base64UrlEncoding));
}

// First part with this MIME type, searching nested multipart containers depth first.
QString findText(const QJsonObject &part, const QString &mimeType)
{
    if (part.value(QStringLiteral("mimeType")).toString() == mimeType) {
        const QString text = decodePart(part);
        if (!text.isEmpty()) {
            return text;
        }
    }
    for (const QJsonValue &child : part.value(QStringLiteral("parts")).toArray()) {
        const QString text = findText(child.toObject(), mimeType);
        if (!text.isEmpty()) {
            return text;
        }
    }
    return {};
}

QString htmlToText(QString html)
{
    html.remove(QRegularExpression(QStringLiteral("<(script|style)[^>]*>.*?</\\1>"),
                                   QRegularExpression::DotMatchesEverythingOption |
                                       QRegularExpression::CaseInsensitiveOption));
    html.replace(QRegularExpression(QStringLiteral("<br\\s*/?>|</(p|div|tr|li|h[1-6])>"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\n"));
    html.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    html.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "))
        .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
        .replace(QStringLiteral("&gt;"), QStringLiteral(">"))
        .replace(QStringLiteral("&quot;"), QStringLiteral("\""))
        .replace(QStringLiteral("&#39;"), QStringLiteral("'"))
        .replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    html.replace(QRegularExpression(QStringLiteral("\n{3,}")), QStringLiteral("\n\n"));
    return html.trimmed();
}

} // namespace

QString bodyText(const QJsonObject &message)
{
    const QJsonObject payload = message.value(QStringLiteral("payload")).toObject();
    const QString plain = findText(payload, QStringLiteral("text/plain"));
    if (!plain.isEmpty()) {
        return plain.trimmed();
    }
    return htmlToText(findText(payload, QStringLiteral("text/html")));
}

} // namespace GmailQuery
