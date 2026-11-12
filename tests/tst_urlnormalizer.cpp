#include <QTest>

#include "PublicSuffixList.h"
#include "UrlNormalizer.h"

class UrlNormalizerTest : public QObject
{
    Q_OBJECT

private slots:
    void normalizes_data();
    void normalizes();
    void rejects_data();
    void rejects();
    void reducesToSiteRoot();
    void registrableDomains_data();
    void registrableDomains();
};

void UrlNormalizerTest::normalizes_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("siteKey");
    QTest::addColumn<QString>("registrable");

    QTest::newRow("bare host gets https")
        << "Example.com" << "https://example.com/" << "example.com" << "example.com";
    QTest::newRow("www dropped from key")
        << "http://www.firma.com.tr/iletisim" << "http://www.firma.com.tr/iletisim"
        << "firma.com.tr" << "firma.com.tr";
    QTest::newRow("fragment removed")
        << "https://a.example.org/x?y=1#top" << "https://a.example.org/x?y=1" << "a.example.org"
        << "example.org";
    QTest::newRow("default port hidden")
        << "https://example.com:443/" << "https://example.com/" << "example.com" << "example.com";
    QTest::newRow("custom port kept")
        << "example.com:8080" << "https://example.com:8080/" << "example.com:8080" << "example.com";
    QTest::newRow("idn to punycode") << "çiçek.com.tr" << "https://xn--iek-1lab.com.tr/"
                                     << "xn--iek-1lab.com.tr" << "xn--iek-1lab.com.tr";
    QTest::newRow("ipv4 has no domain")
        << "http://93.184.216.34" << "http://93.184.216.34/" << "93.184.216.34" << "";
    QTest::newRow("space in path encoded")
        << "example.com/a b" << "https://example.com/a%20b" << "example.com" << "example.com";
}

void UrlNormalizerTest::normalizes()
{
    QFETCH(QString, input);
    QFETCH(QString, url);
    QFETCH(QString, siteKey);
    QFETCH(QString, registrable);
    const UrlNormalizeResult result = normalizeUrl(input, PublicSuffixList::bundled());
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.url, url);
    QCOMPARE(result.value.siteKey, siteKey);
    QCOMPARE(result.value.registrableDomain, registrable);
}

void UrlNormalizerTest::rejects_data()
{
    QTest::addColumn<QString>("input");
    QTest::newRow("empty") << "   ";
    QTest::newRow("ftp") << "ftp://example.com";
    QTest::newRow("credentials") << "https://user:pass@example.com";
    QTest::newRow("no dot") << "localhost";
    QTest::newRow("bad characters") << "exa mple.com";
    QTest::newRow("bad port") << "example.com:99999";
}

void UrlNormalizerTest::rejects()
{
    QFETCH(QString, input);
    QVERIFY(!normalizeUrl(input, PublicSuffixList::bundled()).ok());
}

void UrlNormalizerTest::reducesToSiteRoot()
{
    const UrlNormalizeResult result = normalizeSiteRoot(
        QStringLiteral("https://www.firma.com.tr/urunler/a?b=1"), PublicSuffixList::bundled());
    QVERIFY(result.ok());
    QCOMPARE(result.value.url, QStringLiteral("https://www.firma.com.tr/"));
    QCOMPARE(result.value.siteKey, QStringLiteral("firma.com.tr"));
}

void UrlNormalizerTest::registrableDomains_data()
{
    QTest::addColumn<QString>("host");
    QTest::addColumn<QString>("expected");
    QTest::newRow("com.tr") << "shop.firma.com.tr" << "firma.com.tr";
    QTest::newRow("co.uk") << "a.b.example.co.uk" << "example.co.uk";
    QTest::newRow("gen.tr") << "x.gen.tr" << "x.gen.tr";
    QTest::newRow("suffix itself") << "com.tr" << "";
    QTest::newRow("unknown tld uses last two") << "a.b.unknowntld" << "b.unknowntld";
}

void UrlNormalizerTest::registrableDomains()
{
    QFETCH(QString, host);
    QFETCH(QString, expected);
    QVERIFY(!PublicSuffixList::bundled().isEmpty());
    QCOMPARE(PublicSuffixList::bundled().registrableDomain(host), expected);
}

QTEST_GUILESS_MAIN(UrlNormalizerTest)
#include "tst_urlnormalizer.moc"
