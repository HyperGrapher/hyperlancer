#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWebSocket>
#include <QWebSocketServer>

#include "AppSettings.h"
#include "SecretStore.h"
#include "VisitorStats.h"

// Plays the Convex server: it accepts one connection and sends query results on request.
class FakeConvex : public QObject
{
    Q_OBJECT

public:
    FakeConvex()
        : m_server(QStringLiteral("fake"), QWebSocketServer::NonSecureMode)
    {
        connect(&m_server, &QWebSocketServer::newConnection, this, [this]() {
            m_socket = m_server.nextPendingConnection();
            connect(m_socket, &QWebSocket::textMessageReceived, this, [this](const QString &text) {
                const QJsonObject message = QJsonDocument::fromJson(text.toUtf8()).object();
                if (message.value(QStringLiteral("type")).toString() == QLatin1String("ModifyQuerySet")) {
                    for (const QJsonValue &entry : message.value(QStringLiteral("modifications")).toArray()) {
                        const QJsonObject add = entry.toObject();
                        paths.insert(add.value(QStringLiteral("queryId")).toInt(), add.value(QStringLiteral("udfPath")).toString());
                        args.insert(add.value(QStringLiteral("queryId")).toInt(), add.value(QStringLiteral("args")).toArray().first().toObject());
                    }
                }
            });
        });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }

    // Sends the result of the query that was added for this function.
    void answer(const QString &udfPath, const QJsonValue &value)
    {
        const int id = paths.key(udfPath, -1);
        QVERIFY2(id >= 0, "the client did not subscribe to that function");
        const QJsonObject change{{QStringLiteral("type"), QStringLiteral("QueryUpdated")},
                                 {QStringLiteral("queryId"), id},
                                 {QStringLiteral("value"), value}};
        const QJsonObject transition{{QStringLiteral("type"), QStringLiteral("Transition")},
                                     {QStringLiteral("modifications"), QJsonArray{change}}};
        m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(transition).toJson(QJsonDocument::Compact)));
    }

    QMap<int, QString> paths;
    QMap<int, QJsonObject> args;

private:
    QWebSocketServer m_server;
    QWebSocket *m_socket = nullptr;
};

static QJsonObject session(const QString &id, bool isBot, bool isNew = true)
{
    return {{QStringLiteral("_id"), id},
            {QStringLiteral("visitorId"), QStringLiteral("v-") + id},
            {QStringLiteral("isBot"), isBot},
            {QStringLiteral("isNewVisitor"), isNew},
            {QStringLiteral("city"), QStringLiteral("Adana")},
            {QStringLiteral("startedAt"), static_cast<double>(QDateTime::currentMSecsSinceEpoch() - 5000)},
            {QStringLiteral("lastActivityAt"), static_cast<double>(QDateTime::currentMSecsSinceEpoch())}};
}

class VisitorStatsTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void notConfiguredWithoutKey();
    void filtersBotsLocally();
    void announcesNewPeopleOnly();
    void opensVisitorDetail();
};

void VisitorStatsTest::init()
{
    // The access key is saved through the secret store, which on Windows is the real credential
    // manager of the person running the tests.
    if (SecretStore::isSecureBackend()) {
        QSKIP("This test would overwrite the access key stored in the credential manager.");
    }
}

void VisitorStatsTest::notConfiguredWithoutKey()
{
    QTemporaryDir dir;
    AppSettings settings(dir.filePath(QStringLiteral("s.ini")));
    SecretStore secrets(dir.filePath(QStringLiteral("secrets.ini")));
    VisitorStats stats(secrets, settings);
    QCOMPARE(stats.status(), QStringLiteral("notConfigured"));
    stats.saveConnection(QStringLiteral("https://x-1.convex.cloud"), QString());
    QCOMPARE(stats.status(), QStringLiteral("notConfigured")); // the key is still missing
}

void VisitorStatsTest::filtersBotsLocally()
{
    FakeConvex server;
    QVERIFY(server.listen());
    QTemporaryDir dir;
    AppSettings settings(dir.filePath(QStringLiteral("s.ini")));
    SecretStore secrets(dir.filePath(QStringLiteral("secrets.ini")));
    VisitorStats stats(secrets, settings);
    stats.saveConnection(server.url(), QStringLiteral("secret-key-1234567890"));
    QTRY_VERIFY(server.paths.size() == 2);

    // Bots are asked for in the subscription, whatever the switch says.
    QCOMPARE(server.args.value(server.paths.key(QStringLiteral("stats:recentSessions"))).value(QStringLiteral("includeBots")).toBool(), true);
    QCOMPARE(server.args.value(server.paths.key(QStringLiteral("stats:recentSessions"))).value(QStringLiteral("adminKey")).toString(),
             QStringLiteral("secret-key-1234567890"));

    server.answer(QStringLiteral("stats:recentSessions"), QJsonArray{session(QStringLiteral("a"), false), session(QStringLiteral("b"), true)});
    QTRY_COMPARE(stats.sessions().size(), 1);
    QCOMPARE(stats.botSessionCount(), 1);
    stats.setShowBots(true);
    QCOMPARE(stats.sessions().size(), 2);
}

void VisitorStatsTest::announcesNewPeopleOnly()
{
    FakeConvex server;
    QVERIFY(server.listen());
    QTemporaryDir dir;
    AppSettings settings(dir.filePath(QStringLiteral("s.ini")));
    SecretStore secrets(dir.filePath(QStringLiteral("secrets.ini")));
    VisitorStats stats(secrets, settings);
    QSignalSpy arrived(&stats, &VisitorStats::newVisitor);
    stats.saveConnection(server.url(), QStringLiteral("secret-key-1234567890"));
    QTRY_VERIFY(server.paths.size() == 2);

    // The first data after start-up is history: no notice.
    server.answer(QStringLiteral("stats:recentSessions"), QJsonArray{session(QStringLiteral("old"), false)});
    QTRY_COMPARE(stats.sessions().size(), 1);
    QCOMPARE(arrived.size(), 0);

    // A new person, and a bot that must stay quiet.
    server.answer(QStringLiteral("stats:recentSessions"),
                  QJsonArray{session(QStringLiteral("new"), false), session(QStringLiteral("bot"), true), session(QStringLiteral("old"), false)});
    QTRY_COMPARE(arrived.size(), 1);
    QCOMPARE(arrived.first().first().toMap().value(QStringLiteral("_id")).toString(), QStringLiteral("new"));

    // The same data again does not repeat the notice.
    server.answer(QStringLiteral("stats:recentSessions"),
                  QJsonArray{session(QStringLiteral("new"), false), session(QStringLiteral("bot"), true), session(QStringLiteral("old"), false)});
    QTest::qWait(150);
    QCOMPARE(arrived.size(), 1);

    // A session that was first scored as a bot and is cleared later counts when it is cleared.
    server.answer(QStringLiteral("stats:recentSessions"),
                  QJsonArray{session(QStringLiteral("new"), false), session(QStringLiteral("bot"), false), session(QStringLiteral("old"), false)});
    QTRY_COMPARE(arrived.size(), 2);

    // Switched off in the settings: silent.
    settings.setNotifyVisitors(false);
    server.answer(QStringLiteral("stats:recentSessions"), QJsonArray{session(QStringLiteral("quiet"), false)});
    QTest::qWait(150);
    QCOMPARE(arrived.size(), 2);
}

void VisitorStatsTest::opensVisitorDetail()
{
    FakeConvex server;
    QVERIFY(server.listen());
    QTemporaryDir dir;
    AppSettings settings(dir.filePath(QStringLiteral("s.ini")));
    SecretStore secrets(dir.filePath(QStringLiteral("secrets.ini")));
    VisitorStats stats(secrets, settings);
    stats.saveConnection(server.url(), QStringLiteral("secret-key-1234567890"));
    QTRY_VERIFY(server.paths.size() == 2);

    stats.openVisitor(QStringLiteral("v1"), QStringLiteral("s1"));
    QTRY_VERIFY(server.paths.size() == 4);
    QCOMPARE(server.args.value(server.paths.key(QStringLiteral("stats:visitorDetail"))).value(QStringLiteral("visitorId")).toString(), QStringLiteral("v1"));
    QCOMPARE(server.args.value(server.paths.key(QStringLiteral("stats:sessionEvents"))).value(QStringLiteral("sessionId")).toString(), QStringLiteral("s1"));

    server.answer(QStringLiteral("stats:visitorDetail"),
                  QJsonObject{{QStringLiteral("visitor"), QJsonObject{{QStringLiteral("vid"), QStringLiteral("abc")}}},
                              {QStringLiteral("sessions"), QJsonArray{session(QStringLiteral("s1"), false)}}});
    server.answer(QStringLiteral("stats:sessionEvents"),
                  QJsonObject{{QStringLiteral("capped"), true},
                              {QStringLiteral("events"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("pageview")}}}}});
    QTRY_COMPARE(stats.detailEvents().size(), 1);
    QVERIFY(stats.detailEventsCapped());
    QCOMPARE(stats.detail().value(QStringLiteral("visitor")).toMap().value(QStringLiteral("vid")).toString(), QStringLiteral("abc"));

    stats.closeVisitor();
    QVERIFY(stats.detailVisitorId().isEmpty());
    QVERIFY(stats.detailEvents().isEmpty());
}

QTEST_GUILESS_MAIN(VisitorStatsTest)
#include "tst_visitorstats.moc"
