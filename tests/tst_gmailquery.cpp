#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "GmailQuery.h"

class GmailQueryTest : public QObject
{
    Q_OBJECT

private slots:
    void buildsSearch();
    void freeMailIsNeverMatchedByDomain();
    void extractsAddresses();
    void parsesMetadata();
    void readsPlainBody();
    void fallsBackToHtmlBody();
};

void GmailQueryTest::buildsSearch()
{
    QCOMPARE(GmailQuery::buildSearch({QStringLiteral("Firma.com.tr")},
                                     {QStringLiteral("ali@gmail.com")}),
             QStringLiteral(
                 "from:(@firma.com.tr OR ali@gmail.com) OR to:(@firma.com.tr OR ali@gmail.com) OR "
                 "cc:(@firma.com.tr OR ali@gmail.com)"));
}

void GmailQueryTest::freeMailIsNeverMatchedByDomain()
{
    QVERIFY(
        GmailQuery::buildSearch({QStringLiteral("gmail.com"), QStringLiteral("hotmail.com")}, {})
            .isEmpty());
    QVERIFY(GmailQuery::isFreeMailDomain(QStringLiteral("Yandex.com")));
}

void GmailQueryTest::extractsAddresses()
{
    QCOMPARE(GmailQuery::addressOf(QStringLiteral("Ayşe Yılmaz <Ayse@Firma.com.tr>")),
             QStringLiteral("ayse@firma.com.tr"));
    QCOMPARE(GmailQuery::addressesOf(QStringLiteral("a@x.com, \"B\" <b@y.com.tr>, a@x.com")),
             QStringList({QStringLiteral("a@x.com"), QStringLiteral("b@y.com.tr")}));
}

void GmailQueryTest::parsesMetadata()
{
    const QByteArray json =
        R"({"id":"m1","threadId":"t1","snippet":"Merhaba","internalDate":"1762934400000",
        "payload":{"mimeType":"multipart/mixed","headers":[
          {"name":"From","value":"Me <me@hyper.dev>"},{"name":"To","value":"info@firma.com.tr"},
          {"name":"Cc","value":"x@firma.com.tr"},{"name":"Subject","value":"Web siteniz"}]}})";
    const GmailQuery::MessageMeta sent = GmailQuery::parseMetadata(
        QJsonDocument::fromJson(json).object(), QStringLiteral("ME@hyper.dev"));
    QVERIFY(sent.error.isEmpty());
    QVERIFY(sent.sent);
    QVERIFY(sent.hasAttachments);
    QCOMPARE(sent.subject, QStringLiteral("Web siteniz"));
    QVERIFY(sent.to.contains(QStringLiteral("x@firma.com.tr")));
    QCOMPARE(sent.sentAt.toMSecsSinceEpoch(), 1762934400000LL);

    const QByteArray bounce = R"({"id":"m2","threadId":"t2","payload":{"headers":[
          {"name":"From","value":"Mail Delivery Subsystem <mailer-daemon@googlemail.com>"},
          {"name":"Auto-Submitted","value":"auto-replied"}]}})";
    const GmailQuery::MessageMeta failed = GmailQuery::parseMetadata(
        QJsonDocument::fromJson(bounce).object(), QStringLiteral("me@hyper.dev"));
    QVERIFY(!failed.sent);
    QVERIFY(failed.isBounce);
    QVERIFY(failed.isAutoReply);

    QVERIFY(!GmailQuery::parseMetadata(QJsonObject(), QString()).error.isEmpty());
}

QTEST_GUILESS_MAIN(GmailQueryTest)
void GmailQueryTest::readsPlainBody()
{
    // "Merhaba\nDünya" as base64url inside a nested multipart message.
    const QByteArray plain = QByteArray("Merhaba\nD\xC3\xBCnya").toBase64(QByteArray::Base64UrlEncoding);
    const QByteArray html = QByteArray("<p>Other</p>").toBase64(QByteArray::Base64UrlEncoding);
    const QJsonObject message = QJsonDocument::fromJson(
        QByteArray(R"({"payload":{"mimeType":"multipart/mixed","parts":[{"mimeType":"multipart/alternative","parts":[)"
                   R"({"mimeType":"text/html","body":{"data":")") + html +
        R"("}},{"mimeType":"text/plain","body":{"data":")" + plain + R"("}}]}]}})").object();
    QCOMPARE(GmailQuery::bodyText(message), QString::fromUtf8("Merhaba\nD\xC3\xBCnya"));
}

void GmailQueryTest::fallsBackToHtmlBody()
{
    const QByteArray html =
        QByteArray("<style>p{}</style><p>Hello &amp; welcome</p><p>Bye</p>").toBase64(QByteArray::Base64UrlEncoding);
    const QJsonObject message = QJsonDocument::fromJson(
        QByteArray(R"({"payload":{"mimeType":"text/html","body":{"data":")") + html + R"("}}})").object();
    QCOMPARE(GmailQuery::bodyText(message), QStringLiteral("Hello & welcome\nBye"));
    QVERIFY(GmailQuery::bodyText(QJsonObject()).isEmpty());
}

#include "tst_gmailquery.moc"
