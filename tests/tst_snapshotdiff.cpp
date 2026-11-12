#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "SnapshotDiff.h"

namespace {

QVariantMap summary(const char *json)
{
    return QJsonDocument::fromJson(json).object().toVariantMap();
}

QVariantMap groupNamed(const QVariantMap &result, const QString &section)
{
    for (const QVariant &group : result.value(QStringLiteral("groups")).toList()) {
        if (group.toMap().value(QStringLiteral("section")).toString() == section) {
            return group.toMap();
        }
    }
    return {};
}

} // namespace

class SnapshotDiffTest : public QObject
{
    Q_OBJECT

private slots:
    void identicalSummariesHaveNoChanges();
    void reportsScalarAndSetChanges();
    void matchesTechnologiesByCategoryAndName();
    void formatsDaysAndBooleans();
};

void SnapshotDiffTest::identicalSummariesHaveNoChanges()
{
    const QVariantMap one = summary(R"({"cms":{"name":"WordPress","version":"6.5"},"hosting":{"ips":["1.1.1.1"]}})");
    const QVariantMap result = SnapshotDiff::diff(one, one);
    QCOMPARE(result.value(QStringLiteral("count")).toInt(), 0);
    QVERIFY(result.value(QStringLiteral("groups")).toList().isEmpty());
    QCOMPARE(result.value(QStringLiteral("unchangedSections")).toStringList().size(), 10);
}

void SnapshotDiffTest::reportsScalarAndSetChanges()
{
    const QVariantMap before = summary(R"({"cms":{"name":"WordPress","version":"6.4"},
        "hosting":{"ips":["1.1.1.1","2.2.2.2"]},"seo":{"title":"Old"},"contact":{"emails":["a@x.com"]}})");
    const QVariantMap after = summary(R"({"cms":{"name":"WordPress","version":"6.5"},
        "hosting":{"ips":["2.2.2.2","3.3.3.3"]},"seo":{"title":""},"contact":{"emails":["a@x.com","b@x.com"]}})");
    const QVariantMap result = SnapshotDiff::diff(before, after);

    const QVariantList hosting = groupNamed(result, QStringLiteral("Hosting")).value(QStringLiteral("changes")).toList();
    QCOMPARE(hosting.size(), 3); // IP removed, IP added, CMS version changed
    QStringList kinds;
    for (const QVariant &item : hosting) {
        kinds.append(item.toMap().value(QStringLiteral("kind")).toString());
    }
    QVERIFY(kinds.contains(QStringLiteral("changed")));
    QVERIFY(kinds.contains(QStringLiteral("added")));
    QVERIFY(kinds.contains(QStringLiteral("removed")));

    const QVariantMap title = groupNamed(result, QStringLiteral("SEO")).value(QStringLiteral("changes")).toList().front().toMap();
    QCOMPARE(title.value(QStringLiteral("kind")).toString(), QStringLiteral("removed"));
    QCOMPARE(title.value(QStringLiteral("before")).toString(), QStringLiteral("Old"));

    const QVariantMap email = groupNamed(result, QStringLiteral("Contact")).value(QStringLiteral("changes")).toList().front().toMap();
    QCOMPARE(email.value(QStringLiteral("after")).toString(), QStringLiteral("b@x.com"));
    QCOMPARE(result.value(QStringLiteral("count")).toInt(), 5);
}

void SnapshotDiffTest::matchesTechnologiesByCategoryAndName()
{
    const QVariantMap before = summary(R"({"tech":[{"category":"JS","name":"jQuery","version":"3.5"},
        {"category":"CMS","name":"Joomla","version":null}]})");
    const QVariantMap after = summary(R"({"tech":[{"category":"JS","name":"jQuery","version":"3.7"},
        {"category":"Analytics","name":"GA4","version":null}]})");
    const QVariantList changes = groupNamed(SnapshotDiff::diff(before, after), QStringLiteral("Technologies"))
                                     .value(QStringLiteral("changes"))
                                     .toList();
    QCOMPARE(changes.size(), 3);
    bool sawVersionChange = false;
    for (const QVariant &item : changes) {
        const QVariantMap map = item.toMap();
        if (map.value(QStringLiteral("kind")).toString() == QStringLiteral("changed")) {
            sawVersionChange = true;
            QCOMPARE(map.value(QStringLiteral("before")).toString(), QStringLiteral("3.5"));
            QCOMPARE(map.value(QStringLiteral("after")).toString(), QStringLiteral("3.7"));
        }
    }
    QVERIFY(sawVersionChange);
}

void SnapshotDiffTest::formatsDaysAndBooleans()
{
    const QVariantMap before = summary(R"({"tls":{"validUntil":"2026-01-02T10:00:00Z","verified":true}})");
    const QVariantMap after = summary(R"({"tls":{"validUntil":"2026-04-02T10:00:00Z","verified":false}})");
    const QVariantList changes = groupNamed(SnapshotDiff::diff(before, after), QStringLiteral("SSL / TLS"))
                                     .value(QStringLiteral("changes"))
                                     .toList();
    QCOMPARE(changes.size(), 2);
    QCOMPARE(changes.at(0).toMap().value(QStringLiteral("after")).toString(), QStringLiteral("2026-04-02"));
    QCOMPARE(changes.at(1).toMap().value(QStringLiteral("before")).toString(), QStringLiteral("yes"));
    QCOMPARE(changes.at(1).toMap().value(QStringLiteral("after")).toString(), QStringLiteral("no"));
}

QTEST_GUILESS_MAIN(SnapshotDiffTest)
#include "tst_snapshotdiff.moc"
