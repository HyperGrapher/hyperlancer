#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTest>

#include "JobStore.h"
#include "ProfileIngest.h"
#include "PublicSuffixList.h"
#include "SiteStore.h"
#include "TestDatabase.h"
#include "UrlNormalizer.h"

class ProfileIngestTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void storesSnapshotAndFacts();
    void failedAttemptKeepsLatestProfile();
    void missingReportBecomesFailedSnapshot();
    void addSiteDeduplicatesOnSiteKey();
    void jobQueueDoesNotStackDuplicates();
    void listFiltersAndSearches();

private:
    qint64 addSite(const QString &url);
    QString writeReport(const QJsonObject &report);

    std::unique_ptr<TestDatabase> m_database;
};

void ProfileIngestTest::init()
{
    m_database = std::make_unique<TestDatabase>();
    QVERIFY(m_database->isReady());
}

void ProfileIngestTest::cleanup()
{
    m_database.reset();
}

qint64 ProfileIngestTest::addSite(const QString &url)
{
    QSqlDatabase db = m_database->db();
    const UrlNormalizeResult normalized = normalizeUrl(url, PublicSuffixList::bundled());
    return SiteStore::addSite(db, normalized.value, QStringLiteral("manual")).siteId;
}

QString ProfileIngestTest::writeReport(const QJsonObject &report)
{
    const QString path = QDir(m_database->dir()).filePath(QStringLiteral("profile.json"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qFatal("cannot write report");
    }
    file.write(QJsonDocument(report).toJson());
    return path;
}

namespace {

QJsonObject report(const QString &outcome)
{
    const QJsonObject facts{{QStringLiteral("http_status"), 200},
                            {QStringLiteral("title"), QStringLiteral("Firma")},
                            {QStringLiteral("cms"), QStringLiteral("WordPress")},
                            {QStringLiteral("hosting"), QStringLiteral("Natro")},
                            {QStringLiteral("tls_valid_until"), QStringLiteral("2027-01-02")},
                            {QStringLiteral("not_a_column"), QStringLiteral("ignored")}};
    const QJsonObject summary{
        {QStringLiteral("outcome"), outcome},
        {QStringLiteral("errors"), QJsonArray()},
        {QStringLiteral("url"),
         QJsonObject{{QStringLiteral("requested"), QStringLiteral("https://firma.com.tr/")}}},
        {QStringLiteral("http"), QJsonObject{{QStringLiteral("status"), 200}}},
        {QStringLiteral("tech"),
         QJsonArray{QJsonObject{{QStringLiteral("category"), QStringLiteral("CMS")},
                                {QStringLiteral("name"), QStringLiteral("WordPress")},
                                {QStringLiteral("confidence"), QStringLiteral("high")}}}},
        {QStringLiteral("facts"), facts}};
    return QJsonObject{
        {QStringLiteral("schema_version"), 2},
        {QStringLiteral("status"), outcome},
        {QStringLiteral("metadata"),
         QJsonObject{{QStringLiteral("scanned_at_utc"), QStringLiteral("2026-11-12T08:00:00Z")},
                     {QStringLiteral("duration_ms"), 1234}}},
        {QStringLiteral("summary"), summary}};
}

} // namespace

void ProfileIngestTest::storesSnapshotAndFacts()
{
    QSqlDatabase db = m_database->db();
    const qint64 siteId = addSite(QStringLiteral("firma.com.tr"));
    const ProfileIngest::Result result = ProfileIngest::ingestFile(
        db, siteId, 0, writeReport(report(QStringLiteral("ok"))), QString());
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.outcome, QStringLiteral("ok"));

    QSqlQuery site(db);
    QVERIFY(site.exec(
        QStringLiteral("SELECT cms, hosting, http_status, latest_snapshot_id, last_fetch_outcome, "
                       "tls_valid_until FROM sites")));
    QVERIFY(site.next());
    QCOMPARE(site.value(0).toString(), QStringLiteral("WordPress"));
    QCOMPARE(site.value(1).toString(), QStringLiteral("Natro"));
    QCOMPARE(site.value(2).toInt(), 200);
    QCOMPARE(site.value(3).toLongLong(), result.snapshotId);
    QCOMPARE(site.value(4).toString(), QStringLiteral("ok"));
    QCOMPARE(site.value(5).toString(), QStringLiteral("2027-01-02"));

    QSqlQuery raw(db);
    QVERIFY(raw.exec(QStringLiteral("SELECT raw_json, summary_json FROM profile_snapshots")));
    QVERIFY(raw.next());
    QVERIFY(!raw.value(0).toString().contains(QStringLiteral("\"summary\"")));
    QVERIFY(raw.value(1).toString().contains(QStringLiteral("WordPress")));

    const QVariantMap detail = SiteStore::loadDetail(db, siteId);
    QCOMPARE(detail.value(QStringLiteral("snapshot"))
                 .toMap()
                 .value(QStringLiteral("summary"))
                 .toMap()
                 .value(QStringLiteral("outcome"))
                 .toString(),
             QStringLiteral("ok"));
    QCOMPARE(detail.value(QStringLiteral("activity")).toList().size(), 2); // added + analyzed
}

void ProfileIngestTest::failedAttemptKeepsLatestProfile()
{
    QSqlDatabase db = m_database->db();
    const qint64 siteId = addSite(QStringLiteral("firma.com.tr"));
    const ProfileIngest::Result good = ProfileIngest::ingestFile(
        db, siteId, 0, writeReport(report(QStringLiteral("partial"))), QString());
    const ProfileIngest::Result bad =
        ProfileIngest::ingestFailure(db, siteId, 0, QStringLiteral("unreachable"));
    QCOMPARE(bad.outcome, QStringLiteral("failed"));
    QSqlQuery site(db);
    QVERIFY(
        site.exec(QStringLiteral("SELECT latest_snapshot_id, last_fetch_outcome, cms FROM sites")));
    QVERIFY(site.next());
    QCOMPARE(site.value(0).toLongLong(), good.snapshotId);
    QCOMPARE(site.value(1).toString(), QStringLiteral("failed"));
    QCOMPARE(site.value(2).toString(), QStringLiteral("WordPress"));
}

void ProfileIngestTest::missingReportBecomesFailedSnapshot()
{
    QSqlDatabase db = m_database->db();
    const qint64 siteId = addSite(QStringLiteral("example.org"));
    const ProfileIngest::Result result = ProfileIngest::ingestFile(
        db, siteId, 0, QStringLiteral("/no/such/file.json"), QStringLiteral("crashed"));
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.outcome, QStringLiteral("failed"));
    QSqlQuery snapshot(db);
    QVERIFY(snapshot.exec(QStringLiteral("SELECT error_summary FROM profile_snapshots")));
    QVERIFY(snapshot.next());
    QVERIFY(snapshot.value(0).toString().contains(QStringLiteral("crashed")));
}

void ProfileIngestTest::addSiteDeduplicatesOnSiteKey()
{
    QSqlDatabase db = m_database->db();
    const PublicSuffixList &suffixes = PublicSuffixList::bundled();
    const SiteStore::AddResult first = SiteStore::addSite(
        db, normalizeUrl(QStringLiteral("https://www.firma.com.tr/"), suffixes).value,
        QStringLiteral("manual"));
    const SiteStore::AddResult second = SiteStore::addSite(
        db, normalizeUrl(QStringLiteral("firma.com.tr/iletisim"), suffixes).value,
        QStringLiteral("discovery"));
    QVERIFY(first.created);
    QVERIFY(!second.created);
    QCOMPARE(second.siteId, first.siteId);
    QCOMPARE(
        SiteStore::knownDomains(db, {QStringLiteral("firma.com.tr"), QStringLiteral("other.com")}),
        QSet<QString>({QStringLiteral("firma.com.tr")}));
}

void ProfileIngestTest::jobQueueDoesNotStackDuplicates()
{
    QSqlDatabase db = m_database->db();
    const qint64 siteId = addSite(QStringLiteral("firma.com.tr"));
    const JobStore::CreateResult first =
        JobStore::create(db, QStringLiteral("profile"), siteId, {});
    const JobStore::CreateResult second =
        JobStore::create(db, QStringLiteral("profile"), siteId, {});
    QVERIFY(!first.alreadyQueued);
    QVERIFY(second.alreadyQueued);
    QCOMPARE(second.jobId, first.jobId);
    QCOMPARE(JobStore::queued(db, 10).size(), size_t(1));

    JobStore::markStarted(db, first.jobId, QStringLiteral("jobs/1/job.log"));
    QCOMPARE(JobStore::failInterrupted(db), 1);
    const qint64 retried = JobStore::retry(db, first.jobId);
    QVERIFY(retried > first.jobId);
    QVERIFY(JobStore::cancelQueued(db, retried));
    QCOMPARE(JobStore::recent(db, 10).size(), size_t(2));
}

void ProfileIngestTest::listFiltersAndSearches()
{
    QSqlDatabase db = m_database->db();
    const qint64 analyzed = addSite(QStringLiteral("firma.com.tr"));
    addSite(QStringLiteral("baska.com"));
    QVERIFY(ProfileIngest::ingestFile(db, analyzed, 0, writeReport(report(QStringLiteral("ok"))),
                                      QString())
                .error.isEmpty());

    SiteStore::ListQuery query;
    QCOMPARE(SiteStore::listSites(db, query).rows.size(), size_t(2));
    query.filter = SiteStore::Filter::NeverAnalyzed;
    QCOMPARE(SiteStore::listSites(db, query).rows.size(), size_t(1));
    query.filter = SiteStore::Filter::All;
    query.search = QStringLiteral("WORDPRESS");
    const SiteStore::ListResult found = SiteStore::listSites(db, query);
    QCOMPARE(found.rows.size(), size_t(1));
    QCOMPARE(found.rows.front().siteKey, QStringLiteral("firma.com.tr"));
    query.search = QStringLiteral("100%_");
    QCOMPARE(SiteStore::listSites(db, query).rows.size(), size_t(0));
}

QTEST_GUILESS_MAIN(ProfileIngestTest)
#include "tst_profileingest.moc"
