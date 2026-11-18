#include <QDate>
#include <QDir>
#include <QFile>
#include <QSqlQuery>
#include <QTest>

#include "CrawlStore.h"
#include "JobStore.h"
#include "PublicSuffixList.h"
#include "RenderStore.h"
#include "SiteOrganizer.h"
#include "SiteStore.h"
#include "TestDatabase.h"
#include "UrlNormalizer.h"

class SiteOrganizerTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void statusCategoryAndTags();
    void notesAndTodos();
    void listShowsOrganizerFieldsAndFilters();
    void listSorts();
    void deadlineAndNoteTitle();
    void categoryFilter();
    void archivedSitesLeaveTheListAndComeBack();
    void previewLinkIsSavedAndCleared();
    void mailMovesStatusForwardOnly();
    void screenshotsKeepTheNewestPerKind();
    void speedChecksAreKeptPerStrategy();
    void dashboardListsDueTodosAndDeadlines();
    void diffComparesWithTheEarlierSuccessfulAnalysis();
    void crawlStoreKeepsOnlyTheLatestCrawl();
    void renderStoreSavesScreenshotsAndTrackers();
    void deletingSiteRemovesItsRecords();

private:
    qint64 addSite(const QString &url);

    std::unique_ptr<TestDatabase> m_database;
};

void SiteOrganizerTest::init()
{
    m_database = std::make_unique<TestDatabase>();
    QVERIFY(m_database->isReady());
}

void SiteOrganizerTest::cleanup()
{
    m_database.reset();
}

qint64 SiteOrganizerTest::addSite(const QString &url)
{
    QSqlDatabase db = m_database->db();
    const UrlNormalizeResult normalized = normalizeUrl(url, PublicSuffixList::bundled());
    return SiteStore::addSite(db, normalized.value, QStringLiteral("manual")).siteId;
}

void SiteOrganizerTest::statusCategoryAndTags()
{
    QSqlDatabase db = m_database->db();
    const qint64 site = addSite(QStringLiteral("firma.com.tr"));
    QCOMPARE(SiteOrganizer::load(db, site).value(QStringLiteral("status")).toString(),
             QStringLiteral("none"));

    QVERIFY(SiteOrganizer::setStatus(db, site, QStringLiteral("contacted")));
    QVERIFY(SiteOrganizer::setStatus(db, site, QStringLiteral("waiting")));
    QVERIFY(SiteOrganizer::statuses().indexOf(QStringLiteral("waiting")) < SiteOrganizer::statuses().indexOf(QStringLiteral("replied")));
    QVERIFY(SiteOrganizer::setStatus(db, site, QStringLiteral("contacted")));
    QVERIFY(!SiteOrganizer::setStatus(db, site, QStringLiteral("bogus")));
    QVERIFY(SiteOrganizer::setCategory(db, site, QStringLiteral("  Dental ")));
    QVERIFY(SiteOrganizer::addTag(db, site, QStringLiteral("slow")));
    QVERIFY(SiteOrganizer::addTag(db, site, QStringLiteral("slow"))); // twice: still one link
    QVERIFY(SiteOrganizer::addTag(db, site, QStringLiteral("no ssl")));
    QVERIFY(!SiteOrganizer::addTag(db, site, QStringLiteral("  ")));

    QVariantMap loaded = SiteOrganizer::load(db, site);
    QCOMPARE(loaded.value(QStringLiteral("status")).toString(), QStringLiteral("contacted"));
    QCOMPARE(loaded.value(QStringLiteral("category")).toString(), QStringLiteral("Dental"));
    QCOMPARE(loaded.value(QStringLiteral("tags")).toList().size(), 2);
    QCOMPARE(loaded.value(QStringLiteral("categories")).toStringList(),
             QStringList{QStringLiteral("Dental")});

    const qint64 tagId =
        loaded.value(QStringLiteral("tags")).toList().front().toMap().value(QStringLiteral("id")).toLongLong();
    QVERIFY(SiteOrganizer::removeTag(db, site, tagId));
    QVERIFY(SiteOrganizer::setCategory(db, site, QString()));
    loaded = SiteOrganizer::load(db, site);
    QCOMPARE(loaded.value(QStringLiteral("tags")).toList().size(), 1);
    QVERIFY(loaded.value(QStringLiteral("category")).toString().isEmpty());
}

void SiteOrganizerTest::notesAndTodos()
{
    QSqlDatabase db = m_database->db();
    const qint64 site = addSite(QStringLiteral("firma.com.tr"));
    QVERIFY(SiteOrganizer::addNote(db, site, QStringLiteral("Call"), QStringLiteral("Called, owner is away")));
    QVERIFY(!SiteOrganizer::addNote(db, site, QStringLiteral("t"), QStringLiteral("   ")));
    QVERIFY(SiteOrganizer::addTodo(db, site, QStringLiteral("Late task"), QStringLiteral("2030-05-01")));
    QVERIFY(SiteOrganizer::addTodo(db, site, QStringLiteral("Early task"), QStringLiteral("2030-01-01")));
    QVERIFY(SiteOrganizer::addTodo(db, site, QStringLiteral("Undated task"), QString()));

    QVariantMap loaded = SiteOrganizer::load(db, site);
    QCOMPARE(loaded.value(QStringLiteral("notes")).toList().size(), 1);
    QVariantList todos = loaded.value(QStringLiteral("todos")).toList();
    QCOMPARE(todos.size(), 3);
    QCOMPARE(todos.at(0).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Early task"));
    QCOMPARE(todos.at(2).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Undated task"));

    const qint64 earlyId = todos.at(0).toMap().value(QStringLiteral("id")).toLongLong();
    QVERIFY(SiteOrganizer::setTodoDone(db, earlyId, true));
    todos = SiteOrganizer::load(db, site).value(QStringLiteral("todos")).toList();
    QVERIFY(todos.last().toMap().value(QStringLiteral("done")).toBool()); // finished sinks
    QVERIFY(SiteOrganizer::setTodoDone(db, earlyId, false));
    QVERIFY(SiteOrganizer::deleteTodo(db, earlyId));
    QCOMPARE(SiteOrganizer::load(db, site).value(QStringLiteral("todos")).toList().size(), 2);

    const qint64 noteId = SiteOrganizer::load(db, site)
                              .value(QStringLiteral("notes"))
                              .toList()
                              .front()
                              .toMap()
                              .value(QStringLiteral("id"))
                              .toLongLong();
    QVERIFY(SiteOrganizer::updateNote(db, noteId, QStringLiteral("Called again"), QStringLiteral("Owner is back")));
    QVERIFY(!SiteOrganizer::updateNote(db, noteId, QStringLiteral("x"), QStringLiteral("  "))); // an empty body is refused
    const QVariantMap edited = SiteOrganizer::load(db, site).value(QStringLiteral("notes")).toList().front().toMap();
    QCOMPARE(edited.value(QStringLiteral("title")).toString(), QStringLiteral("Called again"));
    QCOMPARE(edited.value(QStringLiteral("body")).toString(), QStringLiteral("Owner is back"));
    QVERIFY(SiteOrganizer::deleteNote(db, noteId));
    QVERIFY(SiteOrganizer::load(db, site).value(QStringLiteral("notes")).toList().isEmpty());
}

void SiteOrganizerTest::listShowsOrganizerFieldsAndFilters()
{
    QSqlDatabase db = m_database->db();
    const qint64 first = addSite(QStringLiteral("firma.com.tr"));
    addSite(QStringLiteral("baska.com"));
    QVERIFY(SiteOrganizer::setStatus(db, first, QStringLiteral("client")));
    QVERIFY(SiteOrganizer::setCategory(db, first, QStringLiteral("Dental")));
    QVERIFY(SiteOrganizer::addTag(db, first, QStringLiteral("vip")));
    QVERIFY(SiteOrganizer::addTag(db, first, QStringLiteral("local")));
    QVERIFY(SiteOrganizer::addTodo(db, first, QStringLiteral("Send offer"), QString()));

    SiteStore::ListQuery query;
    query.status = QStringLiteral("client");
    SiteStore::ListResult result = SiteStore::listSites(db, query);
    QCOMPARE(result.rows.size(), size_t(1));
    const SiteStore::SiteRow &row = result.rows.front();
    QCOMPARE(row.status, QStringLiteral("client"));
    QCOMPARE(row.category, QStringLiteral("Dental"));
    QCOMPARE(row.tags, QStringLiteral("local, vip"));
    QCOMPARE(row.openTodos, 1);

    query.status.clear();
    query.search = QStringLiteral("vip"); // tag names are searchable
    QCOMPARE(SiteStore::listSites(db, query).rows.size(), size_t(1));
    query.search = QStringLiteral("dental"); // and category names
    QCOMPARE(SiteStore::listSites(db, query).rows.size(), size_t(1));
}

void SiteOrganizerTest::listSorts()
{
    QSqlDatabase db = m_database->db();
    const qint64 alpha = addSite(QStringLiteral("alpha.com"));
    addSite(QStringLiteral("zulu.com"));
    const qint64 middle = addSite(QStringLiteral("mid.com"));
    QVERIFY(SiteOrganizer::setStatus(db, alpha, QStringLiteral("canceled")));
    QVERIFY(SiteOrganizer::setStatus(db, middle, QStringLiteral("contacted")));

    SiteStore::ListQuery query;
    query.sortKey = SiteStore::SortKey::Site;
    query.descending = false;
    SiteStore::ListResult result = SiteStore::listSites(db, query);
    QCOMPARE(result.rows.front().siteKey, QStringLiteral("alpha.com"));
    QCOMPARE(result.rows.back().siteKey, QStringLiteral("zulu.com"));
    query.descending = true;
    QCOMPARE(SiteStore::listSites(db, query).rows.front().siteKey, QStringLiteral("zulu.com"));

    query.sortKey = SiteStore::SortKey::Status; // pipeline order: none, contacted, ..., canceled
    query.descending = false;
    result = SiteStore::listSites(db, query);
    QCOMPARE(result.rows.at(0).status, QStringLiteral("none"));
    QCOMPARE(result.rows.at(1).status, QStringLiteral("contacted"));
    QCOMPARE(result.rows.at(2).status, QStringLiteral("canceled"));

    // Every key must at least produce valid SQL.
    for (int key = 0; key <= static_cast<int>(SiteStore::SortKey::Todos); ++key) {
        query.sortKey = static_cast<SiteStore::SortKey>(key);
        QVERIFY(SiteStore::listSites(db, query).error.isEmpty());
        QCOMPARE(SiteStore::listSites(db, query).rows.size(), size_t(3));
    }
}

void SiteOrganizerTest::deadlineAndNoteTitle()
{
    QSqlDatabase db = m_database->db();
    const qint64 site = addSite(QStringLiteral("firma.com.tr"));
    QVERIFY(SiteOrganizer::setDeadline(db, site, QStringLiteral("2031-02-03")));
    QVERIFY(SiteOrganizer::addNote(db, site, QStringLiteral("Offer"), QStringLiteral("**bold** text")));
    const QVariantMap loaded = SiteOrganizer::load(db, site);
    QCOMPARE(loaded.value(QStringLiteral("deadlineOn")).toString(), QStringLiteral("2031-02-03"));
    const QVariantMap note = loaded.value(QStringLiteral("notes")).toList().front().toMap();
    QCOMPARE(note.value(QStringLiteral("title")).toString(), QStringLiteral("Offer"));

    SiteStore::ListQuery query;
    QCOMPARE(SiteStore::listSites(db, query).rows.front().deadlineOn, QStringLiteral("2031-02-03"));
    query.sortKey = SiteStore::SortKey::Deadline;
    QVERIFY(SiteStore::listSites(db, query).error.isEmpty());
    QVERIFY(SiteOrganizer::setDeadline(db, site, QString()));
    QVERIFY(SiteOrganizer::load(db, site).value(QStringLiteral("deadlineOn")).toString().isEmpty());
}

void SiteOrganizerTest::categoryFilter()
{
    QSqlDatabase db = m_database->db();
    const qint64 first = addSite(QStringLiteral("firma.com.tr"));
    addSite(QStringLiteral("baska.com"));
    QVERIFY(SiteOrganizer::setCategory(db, first, QStringLiteral("Dental")));
    SiteStore::ListQuery query;
    query.category = QStringLiteral("Dental");
    const SiteStore::ListResult result = SiteStore::listSites(db, query);
    QCOMPARE(result.rows.size(), size_t(1));
    QCOMPARE(result.categories, QStringList{QStringLiteral("Dental")});
}

void SiteOrganizerTest::archivedSitesLeaveTheListAndComeBack()
{
    QSqlDatabase db = m_database->db();
    const qint64 first = addSite(QStringLiteral("firma.com.tr"));
    addSite(QStringLiteral("baska.com"));
    QVERIFY(SiteOrganizer::addTodo(db, first, QStringLiteral("Call"), QString()));

    QVERIFY(SiteStore::setArchived(db, first, true));
    SiteStore::ListQuery query;
    QCOMPARE(SiteStore::listSites(db, query).rows.size(), size_t(1));
    query.filter = SiteStore::Filter::Archived;
    const SiteStore::ListResult archived = SiteStore::listSites(db, query);
    QCOMPARE(archived.rows.size(), size_t(1));
    QCOMPARE(archived.rows.front().id, first);
    QVERIFY(!SiteStore::loadDetail(db, first).value(QStringLiteral("site")).toMap().value(QStringLiteral("archived_at")).toString().isEmpty());
    QCOMPARE(SiteStore::dashboard(db).value(QStringLiteral("openTodos")).toInt(), 0); // its to-do is not counted

    QVERIFY(SiteStore::setArchived(db, first, false));
    query.filter = SiteStore::Filter::All;
    QCOMPARE(SiteStore::listSites(db, query).rows.size(), size_t(2));
    query.filter = SiteStore::Filter::Archived;
    QVERIFY(SiteStore::listSites(db, query).rows.empty());
}

void SiteOrganizerTest::previewLinkIsSavedAndCleared()
{
    QSqlDatabase db = m_database->db();
    const qint64 site = addSite(QStringLiteral("firma.com.tr"));
    QVERIFY(SiteStore::loadDetail(db, site).value(QStringLiteral("site")).toMap().value(QStringLiteral("preview_url")).toString().isEmpty());
    QVERIFY(SiteStore::setPreviewUrl(db, site, QStringLiteral("https://firma-redesign.vercel.app/")));
    QCOMPARE(SiteStore::loadDetail(db, site).value(QStringLiteral("site")).toMap().value(QStringLiteral("preview_url")).toString(),
             QStringLiteral("https://firma-redesign.vercel.app/"));
    QVERIFY(SiteStore::setPreviewUrl(db, site, QString()));
    QVERIFY(SiteStore::loadDetail(db, site).value(QStringLiteral("site")).toMap().value(QStringLiteral("preview_url")).toString().isEmpty());
}

void SiteOrganizerTest::mailMovesStatusForwardOnly()
{
    QSqlDatabase db = m_database->db();
    const qint64 site = addSite(QStringLiteral("firma.com.tr"));
    QSqlQuery(db).exec(QStringLiteral("INSERT INTO mail_accounts (email, status, created_at) VALUES ('me@x.com', 'connected', '2030-01-01 00:00:00.000')"));
    const auto addMail = [&db, site](const QString &id, const QString &direction, int bounce, int autoReply) {
        QSqlQuery insert(db);
        insert.prepare(QStringLiteral("INSERT INTO mail_messages (account_id, gmail_id, thread_id, site_id, direction, is_bounce, is_auto_reply) VALUES (1, ?, ?, ?, ?, ?, ?)"));
        insert.addBindValue(id);
        insert.addBindValue(id);
        insert.addBindValue(site);
        insert.addBindValue(direction);
        insert.addBindValue(bounce);
        insert.addBindValue(autoReply);
        QVERIFY(insert.exec());
    };
    const auto status = [&db, site]() { return SiteOrganizer::load(db, site).value(QStringLiteral("status")).toString(); };

    QVERIFY(!SiteOrganizer::applyMailStatus(db, site)); // no mail: nothing happens
    addMail(QStringLiteral("m1"), QStringLiteral("sent"), 0, 0);
    QVERIFY(SiteOrganizer::applyMailStatus(db, site));
    QCOMPARE(status(), QStringLiteral("contacted"));
    addMail(QStringLiteral("m2"), QStringLiteral("received"), 0, 1); // auto-reply: no change
    QVERIFY(!SiteOrganizer::applyMailStatus(db, site));
    addMail(QStringLiteral("m3"), QStringLiteral("received"), 0, 0);
    QVERIFY(SiteOrganizer::applyMailStatus(db, site));
    QCOMPARE(status(), QStringLiteral("replied"));
    QVERIFY(SiteOrganizer::setStatus(db, site, QStringLiteral("client")));
    QVERIFY(!SiteOrganizer::applyMailStatus(db, site)); // never moves back from client
    QCOMPARE(status(), QStringLiteral("client"));
}

void SiteOrganizerTest::screenshotsKeepTheNewestPerKind()
{
    QSqlDatabase db = m_database->db();
    const qint64 site = addSite(QStringLiteral("firma.com.tr"));
    QVERIFY(SiteStore::saveScreenshot(db, m_database->dir(), site, QStringLiteral("mobile"), QStringLiteral("pagespeed"), "one"));
    QVERIFY(SiteStore::saveScreenshot(db, m_database->dir(), site, QStringLiteral("desktop"), QStringLiteral("pagespeed"), "two"));
    QTest::qWait(3); // file names carry a millisecond stamp
    QVERIFY(SiteStore::saveScreenshot(db, m_database->dir(), site, QStringLiteral("mobile"), QStringLiteral("pagespeed"), "three"));
    QVERIFY(!SiteStore::saveScreenshot(db, m_database->dir(), site, QStringLiteral("mobile"), QStringLiteral("pagespeed"), QByteArray()));

    const QVariantMap shots = SiteStore::loadDetail(db, site).value(QStringLiteral("screenshots")).toMap();
    QCOMPARE(shots.size(), 2);
    QFile mobile(QDir(m_database->dir()).filePath(shots.value(QStringLiteral("mobile")).toMap().value(QStringLiteral("path")).toString()));
    QVERIFY(mobile.open(QIODevice::ReadOnly));
    QCOMPARE(mobile.readAll(), QByteArray("three"));
    QCOMPARE(QDir(QDir(m_database->dir()).filePath(QStringLiteral("screenshots/%1").arg(site))).entryList(QDir::Files).size(), 2);
}

void SiteOrganizerTest::speedChecksAreKeptPerStrategy()
{
    QSqlDatabase db = m_database->db();
    const qint64 site = addSite(QStringLiteral("firma.com.tr"));
    SiteStore::saveSpeedCheck(db, site, QStringLiteral("mobile"), 41, {}, QString());
    SiteStore::saveSpeedCheck(db, site, QStringLiteral("desktop"), 88, {}, QString());
    const QVariantMap detail = SiteStore::loadDetail(db, site);
    QCOMPARE(detail.value(QStringLiteral("speed")).toMap().value(QStringLiteral("score")).toInt(), 41);
    QCOMPARE(detail.value(QStringLiteral("speedDesktop")).toMap().value(QStringLiteral("score")).toInt(), 88);
    // Only the mobile score lands on the site row.
    QCOMPARE(detail.value(QStringLiteral("site")).toMap().value(QStringLiteral("mobile_score")).toInt(), 41);
}

void SiteOrganizerTest::dashboardListsDueTodosAndDeadlines()
{
    QSqlDatabase db = m_database->db();
    const qint64 site = addSite(QStringLiteral("firma.com.tr"));
    const QString yesterday = QDate::currentDate().addDays(-1).toString(Qt::ISODate);
    const QString nextWeek = QDate::currentDate().addDays(7).toString(Qt::ISODate);
    const QString farAway = QDate::currentDate().addDays(200).toString(Qt::ISODate);
    QVERIFY(SiteOrganizer::addTodo(db, site, QStringLiteral("Overdue offer"), yesterday));
    QVERIFY(SiteOrganizer::addTodo(db, site, QStringLiteral("Someday"), farAway));
    QVERIFY(SiteOrganizer::addTodo(db, site, QStringLiteral("No date"), QString()));
    QVERIFY(SiteOrganizer::setDeadline(db, site, nextWeek));

    const QVariantMap stats = SiteStore::dashboard(db);
    QCOMPARE(stats.value(QStringLiteral("openTodos")).toInt(), 3);
    QCOMPARE(stats.value(QStringLiteral("overdueTodos")).toInt(), 1);
    const QVariantList coming = stats.value(QStringLiteral("comingUp")).toList();
    QCOMPARE(coming.size(), 2); // the far-away and undated to-dos stay out
    QCOMPARE(coming.at(0).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Overdue offer"));
    QCOMPARE(coming.at(1).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("deadline"));
}

void SiteOrganizerTest::diffComparesWithTheEarlierSuccessfulAnalysis()
{
    QSqlDatabase db = m_database->db();
    const qint64 site = addSite(QStringLiteral("firma.com.tr"));
    const auto addSnapshot = [&db, site](const QString &at, const QString &outcome, const QString &summaryJson) {
        QSqlQuery insert(db);
        insert.prepare(QStringLiteral("INSERT INTO profile_snapshots (site_id, fetched_at, outcome, schema_version, "
                                      "requested_url, summary_json, raw_json, created_at) VALUES (?, ?, ?, 2, 'x', ?, '{}', ?)"));
        insert.addBindValue(site);
        insert.addBindValue(at);
        insert.addBindValue(outcome);
        insert.addBindValue(summaryJson);
        insert.addBindValue(at);
        return insert.exec() ? insert.lastInsertId().toLongLong() : qint64(0);
    };
    const qint64 first = addSnapshot(QStringLiteral("2030-01-01 10:00:00.000"), QStringLiteral("ok"), QStringLiteral(R"({"cms":{"name":"Joomla"}})"));
    addSnapshot(QStringLiteral("2030-01-02 10:00:00.000"), QStringLiteral("failed"), QStringLiteral("{}"));
    const qint64 third = addSnapshot(QStringLiteral("2030-01-03 10:00:00.000"), QStringLiteral("ok"), QStringLiteral(R"({"cms":{"name":"WordPress"}})"));

    QVERIFY(SiteStore::diffWithPrevious(db, first).contains(QStringLiteral("error"))); // nothing earlier
    const QVariantMap result = SiteStore::diffWithPrevious(db, third); // skips the failed one in between
    QVERIFY(!result.contains(QStringLiteral("error")));
    QCOMPARE(result.value(QStringLiteral("count")).toInt(), 1);
    QVERIFY(result.value(QStringLiteral("beforeAt")).toString().startsWith(QStringLiteral("2030-01-01")));
}

void SiteOrganizerTest::crawlStoreKeepsOnlyTheLatestCrawl()
{
    QSqlDatabase db = m_database->db();
    const qint64 site = addSite(QStringLiteral("firma.com.tr"));
    const QString dataDir = m_database->dir();
    const auto writeCrawl = [&dataDir](const QString &job, int pages) {
        const QString folder = QDir(dataDir).filePath(QStringLiteral("jobs/%1/crawl").arg(job));
        QDir().mkpath(folder);
        QFile manifest(folder + QStringLiteral("/manifest.json"));
        manifest.open(QIODevice::WriteOnly);
        manifest.write(QStringLiteral(R"({"status":"partial","stop_reason":"max_pages","counts":{"pages":%1,"images":2,"files":1,"bytes":2048,"errors":0}})")
                           .arg(pages).toUtf8());
        QFile structure(folder + QStringLiteral("/structure.json"));
        structure.open(QIODevice::WriteOnly);
        QFile assets(folder + QStringLiteral("/assets.json"));
        assets.open(QIODevice::WriteOnly);
        assets.write(R"({"assets":[{"kind":"image","file":"images/logo.png","url":"https://firma.com.tr/logo.png","width":10,"height":5,"bytes":70,"alt_texts":["logo"],"used_on":["p1"]},
            {"kind":"file","file":"files/a.pdf","used_on":["p1"]}]})");
        structure.write(R"({"pages":[{"id":"p1","title":"Home","url":"https://firma.com.tr/","file":"pages/index.md","word_count":120,"depth":0,"lang":"tr"}]})");
        return folder + QStringLiteral("/manifest.json");
    };
    QVERIFY(CrawlStore::latest(db, site).isEmpty());

    const QString first = writeCrawl(QStringLiteral("1"), 5);
    QVERIFY(CrawlStore::ingest(db, dataDir, site, 1, first).error.isEmpty());
    QVariantMap latest = CrawlStore::latest(db, site);
    QCOMPARE(latest.value(QStringLiteral("pages")).toInt(), 5);
    QCOMPARE(latest.value(QStringLiteral("stopReason")).toString(), QStringLiteral("max_pages"));
    QCOMPARE(latest.value(QStringLiteral("path")).toString(), QStringLiteral("jobs/1/crawl"));
    const QVariantList pages = CrawlStore::pages(dataDir, latest.value(QStringLiteral("path")).toString());
    QCOMPARE(pages.size(), 1);
    QCOMPARE(pages.front().toMap().value(QStringLiteral("wordCount")).toInt(), 120);
    QCOMPARE(pages.front().toMap().value(QStringLiteral("imageCount")).toInt(), 1); // the PDF is not an image
    const QVariantList images = CrawlStore::images(dataDir, latest.value(QStringLiteral("path")).toString());
    QCOMPARE(images.size(), 1);
    QCOMPARE(images.front().toMap().value(QStringLiteral("pageIds")).toList().front().toString(), QStringLiteral("p1"));
    QVERIFY(images.front().toMap().value(QStringLiteral("file")).toString().endsWith(QStringLiteral("jobs/1/crawl/images/logo.png")));

    const QString second = writeCrawl(QStringLiteral("2"), 9);
    QVERIFY(CrawlStore::ingest(db, dataDir, site, 2, second).error.isEmpty());
    QCOMPARE(CrawlStore::latest(db, site).value(QStringLiteral("pages")).toInt(), 9);
    QVERIFY(!QDir(QDir(dataDir).filePath(QStringLiteral("jobs/1/crawl"))).exists()); // the older crawl is gone
    QVERIFY(QDir(QDir(dataDir).filePath(QStringLiteral("jobs/2/crawl"))).exists());

    QVERIFY(!CrawlStore::ingest(db, dataDir, site, 3, QDir(dataDir).filePath(QStringLiteral("missing.json"))).error.isEmpty());
    CrawlStore::removeFiles(db, dataDir, site);
    QVERIFY(!QDir(QDir(dataDir).filePath(QStringLiteral("jobs/2/crawl"))).exists());
}

void SiteOrganizerTest::renderStoreSavesScreenshotsAndTrackers()
{
    QSqlDatabase db = m_database->db();
    const qint64 site = addSite(QStringLiteral("firma.com.tr"));
    const QString dataDir = m_database->dir();
    QVERIFY(RenderStore::latest(db, site).isEmpty());
    // The jobs table has to accept the job type of the browser render.
    QVERIFY(JobStore::create(db, QStringLiteral("render"), site, {}).error.isEmpty());

    const QString folder = dataDir + QStringLiteral("/jobs/5/render");
    QVERIFY(QDir().mkpath(folder));
    for (const QString &name : {QStringLiteral("desktop"), QStringLiteral("mobile"), QStringLiteral("fullpage")}) {
        QFile shot(folder + QLatin1Char('/') + name + QStringLiteral(".jpg"));
        QVERIFY(shot.open(QIODevice::WriteOnly));
        shot.write(name.toUtf8());
    }
    QFile report(folder + QStringLiteral("/render.json"));
    QVERIFY(report.open(QIODevice::WriteOnly));
    report.write(R"({"title":"Home","loadMs":900,"requestCount":42,
        "trackers":{"items":[{"product":"Meta Pixel","kind":"ads","ids":["123"]}]},
        "files":{"desktop":"desktop.jpg","mobile":"mobile.jpg","fullpage":"fullpage.jpg"}})");
    report.close();

    const QString reportPath = report.fileName();
    QVERIFY(RenderStore::ingest(db, dataDir, site, 5, reportPath).error.isEmpty());
    const QVariantMap render = RenderStore::latest(db, site);
    QCOMPARE(render.value(QStringLiteral("requestCount")).toInt(), 42);
    const QVariantList items = render.value(QStringLiteral("trackers")).toMap().value(QStringLiteral("items")).toList();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.front().toMap().value(QStringLiteral("product")).toString(), QStringLiteral("Meta Pixel"));

    QSqlQuery shots(db);
    QVERIFY(shots.exec(QStringLiteral("SELECT COUNT(*) FROM screenshots WHERE source = 'render'")));
    QVERIFY(shots.next());
    QCOMPARE(shots.value(0).toInt(), 3);
    QVERIFY(!QDir(folder).exists()); // the raw files are gone once stored
    QVERIFY(!RenderStore::ingest(db, dataDir, site, 6, reportPath).error.isEmpty());
}

void SiteOrganizerTest::deletingSiteRemovesItsRecords()
{
    QSqlDatabase db = m_database->db();
    const qint64 site = addSite(QStringLiteral("firma.com.tr"));
    QVERIFY(SiteOrganizer::addTag(db, site, QStringLiteral("vip")));
    QVERIFY(SiteOrganizer::addNote(db, site, QString(), QStringLiteral("note")));
    QVERIFY(SiteOrganizer::addTodo(db, site, QStringLiteral("todo"), QString()));
    QVERIFY(SiteStore::deleteSite(db, site));
    for (const QString &table : {QStringLiteral("site_tags"), QStringLiteral("notes"), QStringLiteral("todos")}) {
        QSqlQuery count(db);
        QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(table)));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), 0);
    }
}

QTEST_GUILESS_MAIN(SiteOrganizerTest)
#include "tst_siteorganizer.moc"
