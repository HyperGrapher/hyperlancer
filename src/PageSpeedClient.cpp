#include "PageSpeedClient.h"

#include <cmath>

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>
#include <QUrlQuery>

namespace PageSpeed {

Result parseResponse(int httpStatus, const QByteArray &body)
{
    Result result;
    result.httpStatus = httpStatus;
    QJsonParseError parseError{};
    const QJsonObject root = QJsonDocument::fromJson(body, &parseError).object();
    if (httpStatus != 200) {
        const QString message = root.value(QStringLiteral("error"))
                                    .toObject()
                                    .value(QStringLiteral("message"))
                                    .toString();
        result.error = httpStatus == 0 ? QObject::tr("PageSpeed Insights could not be reached.")
                                       : QObject::tr("PageSpeed Insights answered with HTTP %1. %2")
                                             .arg(httpStatus)
                                             .arg(message);
        return result;
    }
    if (parseError.error != QJsonParseError::NoError) {
        result.error = QObject::tr("PageSpeed Insights sent a response that is not JSON.");
        return result;
    }
    const QJsonObject lighthouse = root.value(QStringLiteral("lighthouseResult")).toObject();
    result.finalUrl = lighthouse.value(QStringLiteral("finalDisplayedUrl")).toString();
    const QJsonValue score = lighthouse.value(QStringLiteral("categories"))
                                 .toObject()
                                 .value(QStringLiteral("performance"))
                                 .toObject()
                                 .value(QStringLiteral("score"));
    if (score.isDouble()) {
        result.score = static_cast<int>(std::lround(score.toDouble() * 100.0));
    }
    const QJsonObject audits = lighthouse.value(QStringLiteral("audits")).toObject();
    const QList<std::pair<QString, QString>> wanted{
        {QStringLiteral("first-contentful-paint"), QStringLiteral("firstContentfulPaint")},
        {QStringLiteral("largest-contentful-paint"), QStringLiteral("largestContentfulPaint")},
        {QStringLiteral("total-blocking-time"), QStringLiteral("totalBlockingTime")},
        {QStringLiteral("cumulative-layout-shift"), QStringLiteral("cumulativeLayoutShift")},
        {QStringLiteral("speed-index"), QStringLiteral("speedIndex")},
    };
    for (const auto &[audit, name] : wanted) {
        const QJsonObject item = audits.value(audit).toObject();
        if (item.isEmpty()) {
            continue;
        }
        result.metrics.insert(
            name,
            QVariantMap{
                {QStringLiteral("display"), item.value(QStringLiteral("displayValue")).toString()},
                {QStringLiteral("value"), item.value(QStringLiteral("numericValue")).toDouble()},
                {QStringLiteral("score"), item.value(QStringLiteral("score")).toDouble(-1)},
            });
    }
    if (result.score < 0) {
        result.error = QObject::tr("PageSpeed Insights returned no performance score.");
    }
    return result;
}

} // namespace PageSpeed

PageSpeedClient::PageSpeedClient(QNetworkAccessManager &network, QObject *parent)
    : QObject(parent)
    , m_network(network)
{}

void PageSpeedClient::run(const QString &url, const QString &apiKey, const QString &accessToken,
                          std::function<void(PageSpeed::Result)> done)
{
    QUrl endpoint(QStringLiteral("https://www.googleapis.com/pagespeedonline/v5/runPagespeed"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("url"), url);
    query.addQueryItem(QStringLiteral("strategy"), QStringLiteral("mobile"));
    query.addQueryItem(QStringLiteral("category"), QStringLiteral("performance"));
    if (!apiKey.isEmpty()) {
        query.addQueryItem(QStringLiteral("key"), apiKey);
    }
    endpoint.setQuery(query);
    QNetworkRequest request(endpoint);
    if (apiKey.isEmpty() && !accessToken.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
    }
    // A Lighthouse run takes 15-60 seconds.
    request.setTransferTimeout(90000);
    QNetworkReply *reply = m_network.get(request);
    QPointer<PageSpeedClient> self(this);
    connect(reply, &QNetworkReply::finished, this, [self, reply, done = std::move(done)]() {
        reply->deleteLater();
        if (!self) {
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        PageSpeed::Result result = PageSpeed::parseResponse(status, reply->readAll());
        if (status == 0) {
            result.error += QStringLiteral(" ") + reply->errorString();
        }
        done(result);
    });
}
