#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QWebSocket>
#include <QWebSocketServer>

#include "ConvexClient.h"

// A small stand-in for the Convex server: it records what the client sends and answers the way
// the real one was seen to answer.
class FakeServer : public QObject
{
    Q_OBJECT

public:
    FakeServer()
        : m_server(QStringLiteral("fake"), QWebSocketServer::NonSecureMode)
    {
        connect(&m_server, &QWebSocketServer::newConnection, this, [this]() {
            m_socket = m_server.nextPendingConnection();
            ++connections;
            connect(m_socket, &QWebSocket::textMessageReceived, this, [this](const QString &text) {
                received.append(QJsonDocument::fromJson(text.toUtf8()).object());
            });
        });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort())); }
    void sendText(const QString &text) { m_socket->sendTextMessage(text); }
    void dropConnection() { m_socket->close(); }
    QString requestPath() const { return m_socket ? m_socket->requestUrl().path() : QString(); }

    QList<QJsonObject> received;
    int connections = 0;

private:
    QWebSocketServer m_server;
    QWebSocket *m_socket = nullptr;
};

class ConvexClientTest : public QObject
{
    Q_OBJECT

private slots:
    void connectsAndSubscribes();
    void reportsUpdatesAndFailures();
    void joinsChunkedTransitions();
    void resubscribesAfterReconnect();
    void unsubscribeStopsDelivery();
};

static QString transition(const QString &modifications)
{
    return QStringLiteral(R"({"type":"Transition","startVersion":{"querySet":0,"identity":0,"ts":"AAAAAAAAAAA="},)"
                          R"("endVersion":{"querySet":1,"identity":0,"ts":"UVcf3lob6Rg="},"modifications":%1})")
        .arg(modifications);
}

void ConvexClientTest::connectsAndSubscribes()
{
    FakeServer server;
    QVERIFY(server.listen());
    ConvexClient client;
    const int id = client.subscribe(QStringLiteral("stats:recentSessions"), {{QStringLiteral("limit"), 3}});
    client.open(server.url());
    QTRY_COMPARE(client.state(), ConvexClient::State::Connected);
    QTRY_COMPARE(server.received.size(), 2);

    QVERIFY(server.requestPath().startsWith(QStringLiteral("/api/")));
    QVERIFY(server.requestPath().endsWith(QStringLiteral("/sync")));
    const QJsonObject connect = server.received.at(0);
    QCOMPARE(connect.value(QStringLiteral("type")).toString(), QStringLiteral("Connect"));
    QVERIFY(!connect.value(QStringLiteral("sessionId")).toString().isEmpty());
    QCOMPARE(connect.value(QStringLiteral("connectionCount")).toInt(), 0);

    const QJsonObject modify = server.received.at(1);
    QCOMPARE(modify.value(QStringLiteral("type")).toString(), QStringLiteral("ModifyQuerySet"));
    QCOMPARE(modify.value(QStringLiteral("baseVersion")).toInt(), 0);
    QCOMPARE(modify.value(QStringLiteral("newVersion")).toInt(), 1);
    const QJsonObject add = modify.value(QStringLiteral("modifications")).toArray().first().toObject();
    QCOMPARE(add.value(QStringLiteral("queryId")).toInt(), id);
    QCOMPARE(add.value(QStringLiteral("udfPath")).toString(), QStringLiteral("stats:recentSessions"));
    QCOMPARE(add.value(QStringLiteral("args")).toArray().first().toObject().value(QStringLiteral("limit")).toInt(), 3);

    // A later subscription moves the query set one version on.
    client.subscribe(QStringLiteral("stats:overview"), {});
    QTRY_COMPARE(server.received.size(), 3);
    QCOMPARE(server.received.at(2).value(QStringLiteral("baseVersion")).toInt(), 1);
    QCOMPARE(server.received.at(2).value(QStringLiteral("newVersion")).toInt(), 2);
}

void ConvexClientTest::reportsUpdatesAndFailures()
{
    FakeServer server;
    QVERIFY(server.listen());
    ConvexClient client;
    const int ok = client.subscribe(QStringLiteral("a:b"), {});
    const int bad = client.subscribe(QStringLiteral("c:d"), {});
    QSignalSpy updated(&client, &ConvexClient::queryUpdated);
    QSignalSpy failed(&client, &ConvexClient::queryFailed);
    client.open(server.url());
    QTRY_COMPARE(server.received.size(), 2);

    server.sendText(transition(QStringLiteral(R"([{"type":"QueryUpdated","queryId":%1,"value":[{"_id":"x","isBot":false}],"logLines":[],"journal":null},)"
                                              R"({"type":"QueryFailed","queryId":%2,"errorMessage":"Unauthorized","logLines":[],"errorData":null,"journal":null}])")
                                 .arg(ok).arg(bad)));
    QTRY_COMPARE(updated.size(), 1);
    QCOMPARE(updated.first().at(0).toInt(), ok);
    QCOMPARE(updated.first().at(1).value<QJsonValue>().toArray().first().toObject().value(QStringLiteral("_id")).toString(), QStringLiteral("x"));
    QCOMPARE(failed.size(), 1);
    QCOMPARE(failed.first().at(1).toString(), QStringLiteral("Unauthorized"));
}

void ConvexClientTest::joinsChunkedTransitions()
{
    FakeServer server;
    QVERIFY(server.listen());
    ConvexClient client;
    const int id = client.subscribe(QStringLiteral("a:b"), {});
    QSignalSpy updated(&client, &ConvexClient::queryUpdated);
    client.open(server.url());
    QTRY_COMPARE(server.received.size(), 2);

    const QString whole = transition(QStringLiteral(R"([{"type":"QueryUpdated","queryId":%1,"value":{"n":42},"logLines":[],"journal":null}])").arg(id));
    const int half = whole.size() / 2;
    const auto chunk = [&](int part, const QString &text) {
        return QStringLiteral(R"({"type":"TransitionChunk","chunk":%1,"partNumber":%2,"totalParts":2,"transitionId":"t1"})")
            .arg(QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact)).mid(1).chopped(1))
            .arg(part);
    };
    server.sendText(chunk(0, whole.left(half)));
    server.sendText(chunk(1, whole.mid(half)));
    QTRY_COMPARE(updated.size(), 1);
    QCOMPARE(updated.first().at(1).value<QJsonValue>().toObject().value(QStringLiteral("n")).toInt(), 42);
}

void ConvexClientTest::resubscribesAfterReconnect()
{
    FakeServer server;
    QVERIFY(server.listen());
    ConvexClient client;
    client.subscribe(QStringLiteral("a:b"), {});
    client.open(server.url());
    QTRY_COMPARE(server.received.size(), 2);

    server.dropConnection();
    // The client waits a second before it tries again.
    QTRY_COMPARE_WITH_TIMEOUT(server.connections, 2, 5000);
    QTRY_COMPARE(server.received.size(), 4);
    QCOMPARE(server.received.at(2).value(QStringLiteral("connectionCount")).toInt(), 1);
    QCOMPARE(server.received.at(3).value(QStringLiteral("baseVersion")).toInt(), 0);
    QCOMPARE(server.received.at(3).value(QStringLiteral("modifications")).toArray().size(), 1);
}

void ConvexClientTest::unsubscribeStopsDelivery()
{
    FakeServer server;
    QVERIFY(server.listen());
    ConvexClient client;
    const int id = client.subscribe(QStringLiteral("a:b"), {});
    QSignalSpy updated(&client, &ConvexClient::queryUpdated);
    client.open(server.url());
    QTRY_COMPARE(server.received.size(), 2);
    client.unsubscribe(id);
    QTRY_COMPARE(server.received.size(), 3);
    const QJsonObject remove = server.received.at(2).value(QStringLiteral("modifications")).toArray().first().toObject();
    QCOMPARE(remove.value(QStringLiteral("type")).toString(), QStringLiteral("Remove"));

    server.sendText(transition(QStringLiteral(R"([{"type":"QueryUpdated","queryId":%1,"value":1,"logLines":[],"journal":null}])").arg(id)));
    QTest::qWait(200);
    QCOMPARE(updated.size(), 0);
}

QTEST_GUILESS_MAIN(ConvexClientTest)
#include "tst_convexclient.moc"
