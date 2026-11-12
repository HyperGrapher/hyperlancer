#pragma once

#include <functional>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantMap>

class QNetworkAccessManager;

// PageSpeed Insights API v5 (runPagespeed), mobile strategy, performance category.
// Authentication, in order of preference: an API key; an OAuth access token from the same
// Google Cloud project (the Gmail sign-in); or none (works, with a small shared quota).
namespace PageSpeed {

struct Result
{
    int score = -1; // 0..100, -1 when unknown
    QVariantMap
        metrics; // firstContentfulPaint, largestContentfulPaint, ... (display strings and ms)
    QString finalUrl;
    QString error;
    int httpStatus = 0;
};

[[nodiscard]] Result parseResponse(int httpStatus, const QByteArray &body);

} // namespace PageSpeed

class PageSpeedClient : public QObject
{
    Q_OBJECT

public:
    explicit PageSpeedClient(QNetworkAccessManager &network, QObject *parent = nullptr);

    // apiKey and accessToken may be empty. done runs on this object's thread.
    void run(const QString &url, const QString &apiKey, const QString &accessToken,
             std::function<void(PageSpeed::Result)> done);

private:
    QNetworkAccessManager &m_network;
};
