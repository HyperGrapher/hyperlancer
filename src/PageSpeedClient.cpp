#include "PageSpeedClient.h"

#include <cmath>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace PageSpeed {

namespace {

// The machine-readable reason Google attaches to an error (ErrorInfo.reason, or the older
// errors[].reason), e.g. API_KEY_INVALID or SERVICE_DISABLED.
QString errorReason(const QJsonObject &error)
{
    for (const QJsonValue &detail : error.value(QStringLiteral("details")).toArray()) {
        const QString reason = detail.toObject().value(QStringLiteral("reason")).toString();
        if (!reason.isEmpty()) {
            return reason;
        }
    }
    const QJsonArray errors = error.value(QStringLiteral("errors")).toArray();
    return errors.isEmpty() ? QString()
                            : errors.first().toObject().value(QStringLiteral("reason")).toString();
}

QString explainError(int httpStatus, const QJsonObject &error, bool usedKey)
{
    const QString message = error.value(QStringLiteral("message")).toString();
    const QString reason = errorReason(error);
    if (httpStatus == 0) {
        return QObject::tr("PageSpeed Insights could not be reached.");
    }
    if (reason == QStringLiteral("API_KEY_INVALID") ||
        message.contains(QStringLiteral("API key not valid"))) {
        return QObject::tr(
            "The PageSpeed API key is not valid. Replace it in Settings → Connections.");
    }
    if (reason == QStringLiteral("SERVICE_DISABLED") ||
        reason == QStringLiteral("accessNotConfigured")) {
        return QObject::tr(
            "PageSpeed Insights API is not enabled in the Google Cloud project of this "
            "API key. Enable it under APIs & Services → Library.");
    }
    if (reason == QStringLiteral("API_KEY_SERVICE_BLOCKED") ||
        reason == QStringLiteral("API_KEY_HTTP_REFERRER_BLOCKED") ||
        reason == QStringLiteral("API_KEY_IP_ADDRESS_BLOCKED")) {
        return QObject::tr(
            "This API key is restricted and does not allow PageSpeed Insights from this "
            "computer. Check the key's restrictions in Google Cloud → Credentials.");
    }
    if (httpStatus == 429) {
        return usedKey
                   ? QObject::tr("PageSpeed Insights quota of this API key is used up for today.")
                   : QObject::tr("Google's shared PageSpeed quota is used up. Add your own API key "
                                 "in Settings → Connections.");
    }
    if (message.contains(QStringLiteral("Lighthouse returned error"))) {
        return QObject::tr("PageSpeed could not test the page: %1").arg(message);
    }
    if (httpStatus == 400 && usedKey) {
        return QObject::tr(
                   "Google rejected the request (%1). This usually means the saved PageSpeed "
                   "key is not an API key. Replace it in Settings → Connections.")
            .arg(message.isEmpty() ? QStringLiteral("HTTP 400") : message);
    }
    return QObject::tr("PageSpeed Insights answered with HTTP %1. %2").arg(httpStatus).arg(message);
}

} // namespace

QString checkKeyFormat(const QString &key)
{
    const QString value = key.trimmed();
    if (value.startsWith(QStringLiteral("GOCSPX-"))) {
        return QObject::tr(
            "This is an OAuth client secret, not an API key. In Google Cloud → APIs & "
            "Services → Credentials choose Create credentials → API key, and paste that.");
    }
    if (value.contains(QStringLiteral(".apps.googleusercontent.com"))) {
        return QObject::tr(
            "This is an OAuth client ID, not an API key. In Google Cloud → APIs & "
            "Services → Credentials choose Create credentials → API key, and paste that.");
    }
    static const QRegularExpression apiKey(QStringLiteral("^AIza[0-9A-Za-z_-]{35}$"));
    if (!apiKey.match(value).hasMatch()) {
        return QObject::tr("This does not look like a Google API key (they start with \"AIza\" and "
                           "are 39 characters long).");
    }
    return {};
}

Result parseResponse(int httpStatus, const QByteArray &body, bool usedKey)
{
    Result result;
    result.httpStatus = httpStatus;
    QJsonParseError parseError{};
    const QJsonObject root = QJsonDocument::fromJson(body, &parseError).object();
    if (httpStatus != 200) {
        const QJsonObject error = root.value(QStringLiteral("error")).toObject();
        result.error = explainError(httpStatus, error, usedKey);
        qWarning("PageSpeed error HTTP %d (%s): %s", httpStatus,
                 qPrintable(error.value(QStringLiteral("status")).toString()),
                 qPrintable(error.value(QStringLiteral("message")).toString()));
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

void PageSpeedClient::run(const QString &url, const QString &apiKey,
                          std::function<void(PageSpeed::Result)> done)
{
    QUrl endpoint(QStringLiteral("https://www.googleapis.com/pagespeedonline/v5/runPagespeed"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("url"), url);
    query.addQueryItem(QStringLiteral("strategy"), QStringLiteral("mobile"));
    query.addQueryItem(QStringLiteral("category"), QStringLiteral("performance"));
    const bool usedKey = !apiKey.isEmpty();
    if (usedKey) {
        query.addQueryItem(QStringLiteral("key"), apiKey);
    }
    endpoint.setQuery(query);
    QNetworkRequest request(endpoint);
    // A Lighthouse run takes 15-60 seconds.
    request.setTransferTimeout(90000);
    QNetworkReply *reply = m_network.get(request);
    QPointer<PageSpeedClient> self(this);
    connect(
        reply, &QNetworkReply::finished, this, [self, reply, usedKey, done = std::move(done)]() {
            reply->deleteLater();
            if (!self) {
                return;
            }
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            PageSpeed::Result result = PageSpeed::parseResponse(status, reply->readAll(), usedKey);
            if (status == 0) {
                result.error += QStringLiteral(" ") + reply->errorString();
            }
            done(result);
        });
}
