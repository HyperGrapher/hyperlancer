#pragma once

#include <functional>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantMap>

class QNetworkAccessManager;

// PageSpeed Insights API v5 (runPagespeed), performance category, one call per strategy
// (mobile or desktop). Each result carries the Lighthouse final screenshot (a JPEG).
// Authenticated with an API key when one is saved; without a key Google allows anonymous
// calls on a small shared quota. An OAuth client secret or the Gmail sign-in token is not a
// substitute for the key.
namespace PageSpeed {

struct Result
{
    int score = -1; // 0..100, -1 when unknown
    QVariantMap
        metrics; // firstContentfulPaint, largestContentfulPaint, ... (display strings and ms)
    QString finalUrl;
    QByteArray screenshot; // JPEG of the page as Lighthouse saw it; empty when absent
    QString error;
    int httpStatus = 0;
};

// usedKey says whether the request carried an API key, so a rejected request can point at it.
[[nodiscard]] Result parseResponse(int httpStatus, const QByteArray &body, bool usedKey = false);

// Empty when the text looks like a Google API key ("AIza" + 35 characters); otherwise says
// what it is instead (for example an OAuth client secret) and what to create.
[[nodiscard]] QString checkKeyFormat(const QString &key);

} // namespace PageSpeed

class PageSpeedClient : public QObject
{
    Q_OBJECT

public:
    explicit PageSpeedClient(QNetworkAccessManager &network, QObject *parent = nullptr);

    // apiKey may be empty (anonymous call). done runs on this object's thread.
    // strategy is "mobile" or "desktop".
    void run(const QString &url, const QString &apiKey, const QString &strategy,
             std::function<void(PageSpeed::Result)> done);

private:
    QNetworkAccessManager &m_network;
};
