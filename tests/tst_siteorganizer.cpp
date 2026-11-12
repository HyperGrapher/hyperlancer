#include <QSqlQuery>
#include <QTest>

#include "PublicSuffixList.h"
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
             QStringLiteral("new"));

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
    QVERIFY(SiteOrganizer::addNote(db, site, QStringLiteral("Called, owner is away")));
    QVERIFY(!SiteOrganizer::addNote(db, site, QStringLiteral("   ")));
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
    QVERIFY(SiteOrganizer::setStatus(db, alpha, QStringLiteral("rejected")));
    QVERIFY(SiteOrganizer::setStatus(db, middle, QStringLiteral("contacted")));

    SiteStore::ListQuery query;
    query.sortKey = SiteStore::SortKey::Site;
    query.descending = false;
    SiteStore::ListResult result = SiteStore::listSites(db, query);
    QCOMPARE(result.rows.front().siteKey, QStringLiteral("alpha.com"));
    QCOMPARE(result.rows.back().siteKey, QStringLiteral("zulu.com"));
    query.descending = true;
    QCOMPARE(SiteStore::listSites(db, query).rows.front().siteKey, QStringLiteral("zulu.com"));

    query.sortKey = SiteStore::SortKey::Status; // pipeline order: new, contacted, ..., rejected
    query.descending = false;
    result = SiteStore::listSites(db, query);
    QCOMPARE(result.rows.at(0).status, QStringLiteral("new"));
    QCOMPARE(result.rows.at(1).status, QStringLiteral("contacted"));
    QCOMPARE(result.rows.at(2).status, QStringLiteral("rejected"));

    // Every key must at least produce valid SQL.
    for (int key = 0; key <= static_cast<int>(SiteStore::SortKey::Http); ++key) {
        query.sortKey = static_cast<SiteStore::SortKey>(key);
        QVERIFY(SiteStore::listSites(db, query).error.isEmpty());
        QCOMPARE(SiteStore::listSites(db, query).rows.size(), size_t(3));
    }
}

void SiteOrganizerTest::deletingSiteRemovesItsRecords()
{
    QSqlDatabase db = m_database->db();
    const qint64 site = addSite(QStringLiteral("firma.com.tr"));
    QVERIFY(SiteOrganizer::addTag(db, site, QStringLiteral("vip")));
    QVERIFY(SiteOrganizer::addNote(db, site, QStringLiteral("note")));
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
