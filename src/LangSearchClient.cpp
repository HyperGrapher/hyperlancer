#include "LangSearchClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

namespace LangSearch {

namespace {

const QUrl kEndpoint(QStringLiteral("https://api.langsearch.com/v1/web-search"));

QString messageFrom(const QJsonObject &object)
{
    for (const QString &key : {QStringLiteral("message"), QStringLiteral("msg")}) {
        const QString text = object.value(key).toString();
        if (!text.isEmpty()) {
            return text;
        }
    }
    return {};
}

} // namespace

QString cleanText(const QString &text)
{
    QString value = text;
    // Literal JSON escapes that leaked into the text.
    static const QRegularExpression unicodeEscape(QStringLiteral("\\\\u([0-9a-fA-F]{4})"));
    QRegularExpressionMatch match = unicodeEscape.match(value);
    while (match.hasMatch()) {
        const QChar decoded(static_cast<char16_t>(match.captured(1).toUShort(nullptr, 16)));
        value.replace(match.capturedStart(), match.capturedLength(), QString(decoded));
        match = unicodeEscape.match(value, match.capturedStart() + 1);
    }
    value.replace(QStringLiteral("\\/"), QStringLiteral("/"));
    for (const QString &escape :
         {QStringLiteral("\\t"), QStringLiteral("\\n"), QStringLiteral("\\r")}) {
        value.replace(escape, QStringLiteral(" "));
    }
    static const QRegularExpression tags(QStringLiteral("<[^>]{0,200}>"));
    value.remove(tags);
    value.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    value.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    value.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    value.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    value.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    value.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    return value.simplified();
}

QByteArray buildBody(const Request &request)
{
    QJsonObject body{{QStringLiteral("query"), request.query.trimmed()},
                     {QStringLiteral("count"), qBound(1, request.count, 50)},
                     {QStringLiteral("freshness"),
                      request.freshness.isEmpty() ? QStringLiteral("noLimit") : request.freshness}};
    QJsonArray excluded;
    for (const QString &domain : request.excludeDomains) {
        if (!domain.trimmed().isEmpty()) {
            excluded.append(domain.trimmed());
        }
    }
    if (!excluded.isEmpty()) {
        body.insert(QStringLiteral("excludeDomains"), excluded);
    }
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

Response parseResponse(int httpStatus, const QByteArray &body)
{
    Response response;
    response.httpStatus = httpStatus;
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    const QJsonObject root = document.object();
    response.logId = root.value(QStringLiteral("log_id")).toVariant().toString();

    if (httpStatus != 200) {
        const QString detail = messageFrom(root);
        switch (httpStatus) {
        case 400:
            response.error = QObject::tr("The search request was rejected: %1").arg(detail);
            break;
        case 401:
        case 403:
            response.error =
                QObject::tr("LangSearch refused the API key. Check or replace it in Settings.");
            break;
        case 429:
            response.error =
                QObject::tr("LangSearch rate limit or daily allowance reached (resets at "
                            "00:00 UTC). %1")
                    .arg(detail);
            break;
        default:
            response.error = httpStatus == 0 ? QObject::tr("LangSearch could not be reached.")
                                             : QObject::tr("LangSearch answered with HTTP %1. %2")
                                                   .arg(httpStatus)
                                                   .arg(detail);
            response.retryable = httpStatus == 0 || httpStatus >= 500;
        }
        return response;
    }
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        response.error = QObject::tr("LangSearch sent a response that is not JSON.");
        return response;
    }

    const QJsonObject usage = root.value(QStringLiteral("usage")).toObject();
    response.inputTokens = usage.value(QStringLiteral("input_tokens")).toInt();
    response.outputTokens = usage.value(QStringLiteral("output_tokens")).toInt();
    const QJsonArray values = root.value(QStringLiteral("data"))
                                  .toObject()
                                  .value(QStringLiteral("webPages"))
                                  .toObject()
                                  .value(QStringLiteral("value"))
                                  .toArray();
    for (const QJsonValue &value : values) {
        const QJsonObject item = value.toObject();
        Hit hit;
        hit.url = item.value(QStringLiteral("url")).toString();
        if (hit.url.isEmpty()) {
            continue;
        }
        hit.name = cleanText(item.value(QStringLiteral("name")).toString());
        hit.snippet = cleanText(item.value(QStringLiteral("snippet")).toString());
        if (hit.snippet.isEmpty()) {
            hit.snippet =
                cleanText(item.value(QStringLiteral("text")).toString().left(1200)).left(400);
        }
        hit.datePublished = item.value(QStringLiteral("datePublished")).toString();
        response.hits.append(hit);
    }
    return response;
}

} // namespace LangSearch

LangSearchClient::LangSearchClient(QNetworkAccessManager &network, QObject *parent)
    : QObject(parent)
    , m_network(network)
{}

void LangSearchClient::search(const QString &apiKey, const LangSearch::Request &request,
                              std::function<void(LangSearch::Response)> done)
{
    if (apiKey.trimmed().isEmpty()) {
        LangSearch::Response response;
        response.error = tr("No LangSearch API key. Add one in Settings → Connections.");
        done(response);
        return;
    }
    if (request.query.trimmed().isEmpty()) {
        LangSearch::Response response;
        response.error = tr("Enter something to search for.");
        done(response);
        return;
    }
    attempt(apiKey.trimmed(), request, 1, std::move(done));
}

void LangSearchClient::attempt(const QString &apiKey, const LangSearch::Request &request,
                               int attemptNumber, std::function<void(LangSearch::Response)> done)
{
    QNetworkRequest networkRequest(LangSearch::kEndpoint);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             QStringLiteral("application/json"));
    networkRequest.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
    networkRequest.setTransferTimeout(30000);

    QNetworkReply *reply = m_network.post(networkRequest, LangSearch::buildBody(request));
    QPointer<LangSearchClient> self(this);
    connect(reply, &QNetworkReply::finished, this,
            [self, reply, apiKey, request, attemptNumber, done = std::move(done)]() mutable {
                reply->deleteLater();
                if (!self) {
                    return;
                }
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                LangSearch::Response response = LangSearch::parseResponse(status, reply->readAll());
                if (status == 0 && response.error.isEmpty()) {
                    response.error = reply->errorString();
                }
                if (response.retryable && attemptNumber < kMaxAttempts) {
                    // Exponential backoff with jitter: about 1 s, then 2 s.
                    const int delay =
                        (1000 << (attemptNumber - 1)) + QRandomGenerator::global()->bounded(300);
                    QTimer::singleShot(
                        delay, self,
                        [self, apiKey, request, attemptNumber, done = std::move(done)]() mutable {
                            if (self) {
                                self->attempt(apiKey, request, attemptNumber + 1, std::move(done));
                            }
                        });
                    return;
                }
                if (!response.error.isEmpty()) {
                    qWarning("LangSearch error (HTTP %d, log %s): %s", status,
                             qPrintable(response.logId), qPrintable(response.error));
                }
                done(response);
            });
}
