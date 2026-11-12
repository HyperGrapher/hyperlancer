#pragma once

#include <functional>

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

class QNetworkAccessManager;

// LangSearch Web Search API (docs/LangSearch-doc.md): POST /v1/web-search with a Bearer key.
// The key is passed per call, so rotating it in Settings takes effect on the next search.
namespace LangSearch {

struct Hit
{
    QString url;
    QString name;
    QString snippet;
    QString datePublished;
};

struct Request
{
    QString query;
    int count = 20; // 1..50
    QString freshness = QStringLiteral("noLimit");
    QStringList excludeDomains;
};

struct Response
{
    QList<Hit> hits;
    int httpStatus = 0;
    QString error; // empty on success
    QString logId;
    int inputTokens = 0;
    int outputTokens = 0;
    bool retryable = false;
};

[[nodiscard]] QByteArray buildBody(const Request &request);
// Search results sometimes carry page markup, HTML entities and literal JSON escapes (a
// backslash followed by "t", "/" or "u00e9") inside titles and snippets. Returns readable
// single-line text.
[[nodiscard]] QString cleanText(const QString &text);
// Parses an HTTP response. Never throws; malformed bodies give an error text.
[[nodiscard]] Response parseResponse(int httpStatus, const QByteArray &body);

} // namespace LangSearch

class LangSearchClient : public QObject
{
    Q_OBJECT

public:
    static constexpr int kMaxAttempts = 3;

    explicit LangSearchClient(QNetworkAccessManager &network, QObject *parent = nullptr);

    // done runs on this object's thread. Server errors (5xx) and timeouts are retried with
    // backoff; 400/401/403/429 are returned at once.
    void search(const QString &apiKey, const LangSearch::Request &request,
                std::function<void(LangSearch::Response)> done);

private:
    void attempt(const QString &apiKey, const LangSearch::Request &request, int attemptNumber,
                 std::function<void(LangSearch::Response)> done);

    QNetworkAccessManager &m_network;
};
