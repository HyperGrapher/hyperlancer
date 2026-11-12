#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "LangSearchClient.h"
#include "PageSpeedClient.h"

class LangSearchTest : public QObject
{
    Q_OBJECT

private slots:
    void parsesResults();
    void mapsErrors_data();
    void mapsErrors();
    void cleansMarkup();
    void buildsBody();
    void parsesPageSpeed();
    void checksPageSpeedKeyFormat();
    void explainsPageSpeedErrors();
};

void LangSearchTest::parsesResults()
{
    const QByteArray body = R"({"code":200,"log_id":"abc","data":{"webPages":{"value":[
        {"url":"https://firma.com.tr/","name":"Firma <b>A.Ş.</b>","snippet":"Hakkımızda"},
        {"url":"https://b.com/x","name":"B","text":"full text","snippet":null},
        {"name":"no url"}]}},"usage":{"input_tokens":3,"output_tokens":40}})";
    const LangSearch::Response response = LangSearch::parseResponse(200, body);
    QVERIFY(response.error.isEmpty());
    QCOMPARE(response.hits.size(), 2);
    QCOMPARE(response.hits.at(0).name, QStringLiteral("Firma A.Ş."));
    QCOMPARE(response.hits.at(1).snippet, QStringLiteral("full text"));
    QCOMPARE(response.outputTokens, 40);
    QCOMPARE(response.logId, QStringLiteral("abc"));
}

void LangSearchTest::mapsErrors_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<bool>("retryable");
    QTest::newRow("bad request") << 400 << false;
    QTest::newRow("bad key") << 401 << false;
    QTest::newRow("forbidden") << 403 << false;
    QTest::newRow("rate limit") << 429 << false;
    QTest::newRow("server") << 503 << true;
    QTest::newRow("no connection") << 0 << true;
}

void LangSearchTest::mapsErrors()
{
    QFETCH(int, status);
    QFETCH(bool, retryable);
    const LangSearch::Response response = LangSearch::parseResponse(status, R"({"msg":"nope"})");
    QVERIFY(!response.error.isEmpty());
    QCOMPARE(response.retryable, retryable);
}

void LangSearchTest::cleansMarkup()
{
    QCOMPARE(
        LangSearch::cleanText(QStringLiteral("R\\u00e9gimen<\\/h1>\\t\\t\\n Tributario &amp; Co")),
        QStringLiteral("Régimen Tributario & Co"));
    QCOMPARE(LangSearch::cleanText(QStringLiteral("  plain   text ")),
             QStringLiteral("plain text"));
}

void LangSearchTest::buildsBody()
{
    LangSearch::Request request;
    request.query = QStringLiteral("  diş kliniği  ");
    request.count = 500;
    request.excludeDomains = {QStringLiteral("facebook.com"), QStringLiteral(" ")};
    const QJsonObject body = QJsonDocument::fromJson(LangSearch::buildBody(request)).object();
    QCOMPARE(body.value(QStringLiteral("query")).toString(), QStringLiteral("diş kliniği"));
    QCOMPARE(body.value(QStringLiteral("count")).toInt(), 50);
    QCOMPARE(body.value(QStringLiteral("excludeDomains")).toArray().size(), 1);
}

void LangSearchTest::parsesPageSpeed()
{
    const QByteArray body = R"({"lighthouseResult":{"finalDisplayedUrl":"https://a.com/",
        "categories":{"performance":{"score":0.42}},
        "audits":{"largest-contentful-paint":{"displayValue":"5.1 s","numericValue":5100,"score":0.2}}}})";
    const PageSpeed::Result result = PageSpeed::parseResponse(200, body);
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.score, 42);
    QCOMPARE(result.metrics.value(QStringLiteral("largestContentfulPaint"))
                 .toMap()
                 .value(QStringLiteral("display"))
                 .toString(),
             QStringLiteral("5.1 s"));
    const PageSpeed::Result quota =
        PageSpeed::parseResponse(429, R"({"error":{"message":"Quota exceeded"}})");
    QVERIFY(quota.error.contains(QStringLiteral("quota")));
    QCOMPARE(quota.score, -1);
}

void LangSearchTest::checksPageSpeedKeyFormat()
{
    QVERIFY(PageSpeed::checkKeyFormat(QStringLiteral(" AIzaSyA1b2C3d4E5f6G7h8I9j0K1l2M3n4O5p6Q "))
                .isEmpty());
    QVERIFY(PageSpeed::checkKeyFormat(QStringLiteral("GOCSPX-abcdefghijklmnop"))
                .contains(QStringLiteral("client secret")));
    QVERIFY(PageSpeed::checkKeyFormat(QStringLiteral("123-abc.apps.googleusercontent.com"))
                .contains(QStringLiteral("client ID")));
    QVERIFY(!PageSpeed::checkKeyFormat(QStringLiteral("not-a-key")).isEmpty());
}

void LangSearchTest::explainsPageSpeedErrors()
{
    const QByteArray invalidKey =
        R"({"error":{"code":400,"message":"API key not valid. Please pass a valid API key.",
        "status":"INVALID_ARGUMENT","details":[{"@type":"type.googleapis.com/google.rpc.ErrorInfo","reason":"API_KEY_INVALID"}]}})";
    QVERIFY(PageSpeed::parseResponse(400, invalidKey, true)
                .error.contains(QStringLiteral("not valid")));

    const QByteArray disabled =
        R"({"error":{"code":403,"message":"PageSpeed Insights API has not been used",
        "status":"PERMISSION_DENIED","details":[{"reason":"SERVICE_DISABLED"}]}})";
    QVERIFY(PageSpeed::parseResponse(403, disabled, true)
                .error.contains(QStringLiteral("not enabled")));

    const QByteArray generic =
        R"({"error":{"code":400,"message":"Request contains an invalid argument.","status":"INVALID_ARGUMENT"}})";
    QVERIFY(PageSpeed::parseResponse(400, generic, true)
                .error.contains(QStringLiteral("not an API key")));

    const QByteArray quota =
        R"({"error":{"code":429,"message":"Quota exceeded","status":"RESOURCE_EXHAUSTED"}})";
    QVERIFY(PageSpeed::parseResponse(429, quota, false).error.contains(QStringLiteral("shared")));
}

QTEST_GUILESS_MAIN(LangSearchTest)
#include "tst_langsearch.moc"
