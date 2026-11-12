#include <QTest>

#include "JsonLines.h"

class JsonLinesTest : public QObject
{
    Q_OBJECT

private slots:
    void joinsChunksIntoLines();
    void rejectsInvalidLines();
    void finishParsesTrailingLine();
    void dropsOverlongLine();
};

void JsonLinesTest::joinsChunksIntoLines()
{
    JsonLines lines;
    JsonLines::Batch first = lines.feed(R"({"type":"progress","cur)");
    QVERIFY(first.events.isEmpty());
    JsonLines::Batch second = lines.feed("rent\":1}\r\n{\"type\":\"result\",\"path\":\"a\"}\n");
    QCOMPARE(second.events.size(), 2);
    QCOMPARE(second.events.at(0).value(QStringLiteral("current")).toInt(), 1);
    QCOMPARE(second.events.at(1).value(QStringLiteral("path")).toString(), QStringLiteral("a"));
}

void JsonLinesTest::rejectsInvalidLines()
{
    JsonLines lines;
    const JsonLines::Batch batch =
        lines.feed("Traceback (most recent call last)\n[1,2]\n\n{\"type\":\"log\"}\n");
    QCOMPARE(batch.events.size(), 1);
    QCOMPARE(batch.rejected.size(), 2);
}

void JsonLinesTest::finishParsesTrailingLine()
{
    JsonLines lines;
    QVERIFY(lines.feed("{\"type\":\"result\"}").events.isEmpty());
    QCOMPARE(lines.finish().events.size(), 1);
    QVERIFY(lines.finish().events.isEmpty());
}

void JsonLinesTest::dropsOverlongLine()
{
    JsonLines lines;
    const JsonLines::Batch batch = lines.feed(QByteArray(JsonLines::kMaxLineBytes + 10, 'x'));
    QCOMPARE(batch.rejected.size(), 1);
    QCOMPARE(lines.feed("{\"ok\":true}\n").events.size(), 1);
}

QTEST_GUILESS_MAIN(JsonLinesTest)
#include "tst_jsonlines.moc"
