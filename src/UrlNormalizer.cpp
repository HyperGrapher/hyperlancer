#include "UrlNormalizer.h"

#include <QHostAddress>
#include <QObject>
#include <QRegularExpression>
#include <QUrl>

#include "PublicSuffixList.h"

namespace {

UrlNormalizeResult failure(const QString &message)
{
    UrlNormalizeResult result;
    result.error = message;
    return result;
}

QString percentEncodePath(const QString &path)
{
    static const QByteArray kAllowed = QByteArrayLiteral("-._~!$&'()*+,;=:@/?%");
    return QString::fromLatin1(QUrl::toPercentEncoding(path, kAllowed));
}

} // namespace

UrlNormalizeResult normalizeUrl(const QString &input, const PublicSuffixList &suffixes)
{
    const QString value = input.trimmed();
    if (value.isEmpty()) {
        return failure(QObject::tr("Enter a website URL or hostname."));
    }

    static const QRegularExpression schemePattern(QStringLiteral("^([a-z][a-z0-9+.-]*)://"),
                                                  QRegularExpression::CaseInsensitiveOption);
    QString scheme = QStringLiteral("https");
    QString rest;
    if (const QRegularExpressionMatch match = schemePattern.match(value); match.hasMatch()) {
        scheme = match.captured(1).toLower();
        if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
            return failure(QObject::tr("Only http and https URLs are supported."));
        }
        rest = value.mid(match.capturedLength());
    } else {
        rest = value;
        while (rest.startsWith(u'/')) {
            rest.remove(0, 1);
        }
    }

    const qsizetype hashAt = rest.indexOf(u'#');
    if (hashAt >= 0) {
        rest.truncate(hashAt);
    }
    qsizetype authorityEnd = rest.size();
    for (const QChar stop : {QChar(u'/'), QChar(u'?')}) {
        const qsizetype at = rest.indexOf(stop);
        if (at >= 0 && at < authorityEnd) {
            authorityEnd = at;
        }
    }
    QString authority = rest.left(authorityEnd);
    const QString pathAndQuery = rest.mid(authorityEnd);
    if (authority.contains(u'@')) {
        return failure(QObject::tr("URLs containing a username or password are not supported."));
    }

    int port = -1;
    QString host;
    QString shownHost;
    bool isIp = false;
    static const QRegularExpression ipv6Pattern(
        QStringLiteral("^\\[([0-9a-fA-F:.]+)\\](?::(\\d{1,5}))?$"));
    if (const QRegularExpressionMatch match = ipv6Pattern.match(authority); match.hasMatch()) {
        host = match.captured(1).toLower();
        if (!match.captured(2).isEmpty()) {
            port = match.captured(2).toInt();
        }
        const QHostAddress address(host);
        if (address.isNull() || address.protocol() != QAbstractSocket::IPv6Protocol) {
            return failure(QObject::tr("Enter a valid IPv6 address."));
        }
        isIp = true;
        shownHost = QStringLiteral("[%1]").arg(host);
    } else {
        static const QRegularExpression portPattern(QStringLiteral("^(.*):(\\d{1,5})$"));
        if (const QRegularExpressionMatch match = portPattern.match(authority); match.hasMatch()) {
            authority = match.captured(1);
            port = match.captured(2).toInt();
        }
        host = authority.toLower();
        while (host.endsWith(u'.')) {
            host.chop(1);
        }
        const QHostAddress address(host);
        isIp = !address.isNull() && address.protocol() == QAbstractSocket::IPv4Protocol;
        if (!isIp) {
            host = QString::fromLatin1(QUrl::toAce(host)).toLower();
            static const QRegularExpression hostPattern(
                QStringLiteral("^(?=.{1,253}$)([a-z0-9_]([a-z0-9_-]{0,61}[a-z0-9_])?)(\\.[a-z0-9_]("
                               "[a-z0-9_-]{0,61}[a-z0-9_])?)*$"));
            if (host.isEmpty() || !hostPattern.match(host).hasMatch() || !host.contains(u'.')) {
                return failure(QObject::tr("Enter a valid hostname."));
            }
        }
        shownHost = host;
    }
    if (port != -1 && (port < 1 || port > 65535)) {
        return failure(QObject::tr("Enter a valid port."));
    }

    QString path = pathAndQuery.isEmpty() ? QStringLiteral("/") : pathAndQuery;
    if (!path.startsWith(u'/')) {
        path.prepend(u'/');
    }
    path = percentEncodePath(path);

    const int defaultPort = scheme == QStringLiteral("https") ? 443 : 80;
    const QString portSuffix =
        port != -1 && port != defaultPort ? QStringLiteral(":%1").arg(port) : QString();

    UrlNormalizeResult result;
    result.value.url = scheme + QStringLiteral("://") + shownHost + portSuffix + path;
    result.value.host = host;
    result.value.isIp = isIp;
    result.value.siteKey =
        (!isIp && host.startsWith(QStringLiteral("www.")) ? host.mid(4) : host) + portSuffix;
    result.value.registrableDomain = isIp ? QString() : suffixes.registrableDomain(host);
    return result;
}

UrlNormalizeResult normalizeSiteRoot(const QString &input, const PublicSuffixList &suffixes)
{
    UrlNormalizeResult result = normalizeUrl(input, suffixes);
    if (!result.ok()) {
        return result;
    }
    const QUrl url(result.value.url);
    QUrl root;
    root.setScheme(url.scheme());
    root.setHost(url.host());
    root.setPort(url.port());
    root.setPath(QStringLiteral("/"));
    result.value.url = root.toString();
    return result;
}
