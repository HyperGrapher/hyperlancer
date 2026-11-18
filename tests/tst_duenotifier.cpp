#include <QDate>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "AppSettings.h"
#include "Database.h"
#include "DueNotifier.h"
#include "PublicSuffixList.h"
#include "SiteOrganizer.h"
#include "SiteStore.h"
#include "UrlNormalizer.h"

class DueNotifierTest : public QObject
{
    Q_OBJECT

private slots:
    void announcesDueAndOverdueOncePerDay();
    void staysQuietWhenSwitchedOff();
};

namespace {

qint64 addSite(Database &database, const QString &url)
{
    return database.runBlocking([url](QSqlDatabase &db) {
        const UrlNormalizeResult normalized = normalizeUrl(url, PublicSuffixList::bundled());
        return SiteStore::addSite(db, normalized.value, QStringLiteral("manual")).siteId;
    });
}

} // namespace

void DueNotifierTest::announcesDueAndOverdueOncePerDay()
{
    QTemporaryDir dir;
    DatabaseConfig config;
    config.sqlitePath = dir.filePath(QStringLiteral("t.sqlite"));
    Database database;
    database.start(config);
    QTRY_COMPARE_WITH_TIMEOUT(database.state(), Database::State::Ready, 15000);
    AppSettings settings(dir.filePath(QStringLiteral("s.ini")));

    const QString today = QDate::currentDate().toString(Qt::ISODate);
    const QString yesterday = QDate::currentDate().addDays(-1).toString(Qt::ISODate);
    const QString tomorrow = QDate::currentDate().addDays(1).toString(Qt::ISODate);
    const qint64 first = addSite(database, QStringLiteral("firma.com.tr"));
    const qint64 second = addSite(database, QStringLiteral("baska.com"));
    const qint64 finished = addSite(database, QStringLiteral("bitti.com"));
    database.runBlocking([&](QSqlDatabase &db) {
        SiteOrganizer::setDeadline(db, first, today);
        SiteOrganizer::setDeadline(db, finished, yesterday);
        SiteOrganizer::setStatus(db, finished, QStringLiteral("completed"));
        SiteOrganizer::addTodo(db, second, QStringLiteral("Overdue"), yesterday);
        SiteOrganizer::addTodo(db, second, QStringLiteral("Later"), tomorrow);
        SiteOrganizer::addTodo(db, second, QStringLiteral("No date"), QString());
        SiteOrganizer::addTodo(db, second, QStringLiteral("Done"), yesterday);
        return true;
    });
    // Mark "Done" as done.
    database.runBlocking([&](QSqlDatabase &db) {
        QSqlQuery query(db);
        query.exec(QStringLiteral("SELECT id FROM todos WHERE title = 'Done'"));
        query.next();
        return SiteOrganizer::setTodoDone(db, query.value(0).toLongLong(), true);
    });

    DueNotifier notifier(database, settings);
    QSignalSpy spy(&notifier, &DueNotifier::dueItems);
    notifier.check();
    QTRY_COMPARE(spy.size(), 1);
    const QVariantList items = spy.first().first().toList();
    QCOMPARE(items.size(), 2); // the overdue to-do and today's deadline; not the finished site, the future or undated or done ones
    QStringList titles;
    for (const QVariant &item : items) {
        const QVariantMap map = item.toMap();
        titles.append(map.value(QStringLiteral("kind")).toString() + QLatin1Char(':') + map.value(QStringLiteral("title")).toString()
                      + QLatin1Char(':') + (map.value(QStringLiteral("overdue")).toBool() ? QStringLiteral("overdue") : QStringLiteral("today")));
    }
    titles.sort();
    QCOMPARE(titles, (QStringList{QStringLiteral("deadline::today"), QStringLiteral("todo:Overdue:overdue")}));

    // Checking again the same day announces nothing, even for a new notifier (a restart).
    notifier.check();
    DueNotifier restarted(database, settings);
    QSignalSpy again(&restarted, &DueNotifier::dueItems);
    restarted.check();
    QTest::qWait(300);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(again.size(), 0);
}

void DueNotifierTest::staysQuietWhenSwitchedOff()
{
    QTemporaryDir dir;
    DatabaseConfig config;
    config.sqlitePath = dir.filePath(QStringLiteral("t.sqlite"));
    Database database;
    database.start(config);
    QTRY_COMPARE_WITH_TIMEOUT(database.state(), Database::State::Ready, 15000);
    AppSettings settings(dir.filePath(QStringLiteral("s.ini")));
    settings.setNotifyDue(false);
    const qint64 site = addSite(database, QStringLiteral("firma.com.tr"));
    database.runBlocking([&](QSqlDatabase &db) {
        return SiteOrganizer::setDeadline(db, site, QDate::currentDate().toString(Qt::ISODate));
    });
    DueNotifier notifier(database, settings);
    QSignalSpy spy(&notifier, &DueNotifier::dueItems);
    notifier.check();
    QTest::qWait(300);
    QCOMPARE(spy.size(), 0);
}

QTEST_GUILESS_MAIN(DueNotifierTest)
#include "tst_duenotifier.moc"
