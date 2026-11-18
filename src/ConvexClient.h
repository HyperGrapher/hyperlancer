#pragma once

#include <map>

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

// A minimal client for the Convex sync protocol: it keeps live query subscriptions open over a
// WebSocket and reports every new result. No mutations, actions or sign-in: the queries it is
// used for carry their own access key in their arguments. The protocol is internal to Convex
// (the client library for JavaScript is the reference), so the version in the address is pinned.
class ConvexClient : public QObject
{
    Q_OBJECT

public:
    enum class State { Disconnected, Connecting, Connected };
    Q_ENUM(State)

    explicit ConvexClient(QObject *parent = nullptr);

    [[nodiscard]] State state() const { return m_state; }

    // Connects to https://<name>.convex.cloud and keeps reconnecting until close() is called.
    void open(const QUrl &deploymentUrl);
    void close();

    // Returns an id for queryUpdated(). Subscriptions survive reconnects.
    int subscribe(const QString &udfPath, const QJsonObject &args);
    void unsubscribe(int queryId);

signals:
    void stateChanged();
    void queryUpdated(int queryId, const QJsonValue &value);
    void queryFailed(int queryId, const QString &message);

private:
    struct Query
    {
        QString path;
        QJsonObject args;
    };

    void connectNow();
    void onConnected();
    void onDisconnected();
    void onText(const QString &text);
    void handleTransition(const QJsonObject &message);
    void send(const QJsonObject &message);
    void sendAllQueries();
    void setState(State state);
    void scheduleReconnect();

    QUrl m_socketUrl;
    QWebSocket m_socket;
    State m_state = State::Disconnected;
    bool m_wanted = false;
    std::map<int, Query> m_queries;
    int m_nextQueryId = 0;
    int m_querySetVersion = 0;
    int m_connectionCount = 0;
    QString m_sessionId;
    QString m_lastCloseReason;
    QTimer m_reconnectTimer;
    int m_backoffMs = 1000;
    // Convex sends nothing for a minute only when the connection is dead.
    QTimer m_inactivityTimer;
    // A big result arrives in parts that are joined in order.
    QString m_chunkTransitionId;
    QString m_chunkBuffer;
    int m_chunksSeen = 0;
};
