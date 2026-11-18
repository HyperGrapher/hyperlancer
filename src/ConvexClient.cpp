#include "ConvexClient.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

namespace {

constexpr auto kClientVersion = "1.49.0";
constexpr int kMaxBackoffMs = 30000;
constexpr int kInactivityMs = 60000;

} // namespace

ConvexClient::ConvexClient(QObject *parent)
    : QObject(parent)
    , m_sessionId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_reconnectTimer.setSingleShot(true);
    m_inactivityTimer.setSingleShot(true);
    m_inactivityTimer.setInterval(kInactivityMs);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &ConvexClient::connectNow);
    connect(&m_inactivityTimer, &QTimer::timeout, this, [this]() { m_socket.abort(); });
    connect(&m_socket, &QWebSocket::connected, this, &ConvexClient::onConnected);
    connect(&m_socket, &QWebSocket::disconnected, this, &ConvexClient::onDisconnected);
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &ConvexClient::onText);
}

void ConvexClient::open(const QUrl &deploymentUrl)
{
    close();
    QUrl url = deploymentUrl;
    url.setScheme(deploymentUrl.scheme() == QLatin1String("http") ? QStringLiteral("ws") : QStringLiteral("wss"));
    url.setPath(QStringLiteral("/api/%1/sync").arg(QLatin1String(kClientVersion)));
    m_socketUrl = url;
    m_wanted = true;
    m_backoffMs = 1000;
    connectNow();
}

void ConvexClient::close()
{
    m_wanted = false;
    m_reconnectTimer.stop();
    m_inactivityTimer.stop();
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.abort();
    }
    setState(State::Disconnected);
}

int ConvexClient::subscribe(const QString &udfPath, const QJsonObject &args)
{
    const int id = m_nextQueryId++;
    m_queries[id] = {udfPath, args};
    if (m_state == State::Connected) {
        const int base = m_querySetVersion++;
        send({{QStringLiteral("type"), QStringLiteral("ModifyQuerySet")},
              {QStringLiteral("baseVersion"), base},
              {QStringLiteral("newVersion"), m_querySetVersion},
              {QStringLiteral("modifications"),
               QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("Add")},
                                      {QStringLiteral("queryId"), id},
                                      {QStringLiteral("udfPath"), udfPath},
                                      {QStringLiteral("args"), QJsonArray{args}}}}}});
    }
    return id;
}

void ConvexClient::unsubscribe(int queryId)
{
    const bool known = m_queries.erase(queryId) > 0;
    if (!known || m_state != State::Connected) {
        return;
    }
    const int base = m_querySetVersion++;
    send({{QStringLiteral("type"), QStringLiteral("ModifyQuerySet")},
          {QStringLiteral("baseVersion"), base},
          {QStringLiteral("newVersion"), m_querySetVersion},
          {QStringLiteral("modifications"),
           QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("Remove")},
                                  {QStringLiteral("queryId"), queryId}}}}});
}

void ConvexClient::connectNow()
{
    if (!m_wanted || m_socket.state() != QAbstractSocket::UnconnectedState) {
        return;
    }
    setState(State::Connecting);
    m_inactivityTimer.start();
    m_socket.open(m_socketUrl);
}

void ConvexClient::onConnected()
{
    m_backoffMs = 1000;
    m_chunkTransitionId.clear();
    m_chunkBuffer.clear();
    m_chunksSeen = 0;
    m_inactivityTimer.start();
    send({{QStringLiteral("type"), QStringLiteral("Connect")},
          {QStringLiteral("sessionId"), m_sessionId},
          {QStringLiteral("connectionCount"), m_connectionCount++},
          {QStringLiteral("lastCloseReason"), m_lastCloseReason.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(m_lastCloseReason)},
          {QStringLiteral("clientTs"), static_cast<double>(QDateTime::currentMSecsSinceEpoch())}});
    m_querySetVersion = 0;
    sendAllQueries();
    setState(State::Connected);
}

void ConvexClient::sendAllQueries()
{
    if (m_queries.empty()) {
        return;
    }
    QJsonArray adds;
    for (const auto &[id, query] : m_queries) {
        adds.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("Add")},
                                {QStringLiteral("queryId"), id},
                                {QStringLiteral("udfPath"), query.path},
                                {QStringLiteral("args"), QJsonArray{query.args}}});
    }
    send({{QStringLiteral("type"), QStringLiteral("ModifyQuerySet")},
          {QStringLiteral("baseVersion"), 0},
          {QStringLiteral("newVersion"), 1},
          {QStringLiteral("modifications"), adds}});
    m_querySetVersion = 1;
}

void ConvexClient::onDisconnected()
{
    m_inactivityTimer.stop();
    m_lastCloseReason = m_socket.closeReason();
    if (m_lastCloseReason.isEmpty()) {
        m_lastCloseReason = m_socket.errorString();
    }
    setState(State::Disconnected);
    scheduleReconnect();
}

void ConvexClient::scheduleReconnect()
{
    if (!m_wanted) {
        return;
    }
    m_reconnectTimer.start(m_backoffMs);
    m_backoffMs = qMin(m_backoffMs * 2, kMaxBackoffMs);
}

void ConvexClient::onText(const QString &text)
{
    m_inactivityTimer.start();
    const QJsonObject message = QJsonDocument::fromJson(text.toUtf8()).object();
    const QString type = message.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("Transition")) {
        handleTransition(message);
    } else if (type == QLatin1String("TransitionChunk")) {
        const QString id = message.value(QStringLiteral("transitionId")).toString();
        const int part = message.value(QStringLiteral("partNumber")).toInt(-1);
        const int total = message.value(QStringLiteral("totalParts")).toInt();
        if (part == 0) {
            m_chunkTransitionId = id;
            m_chunkBuffer.clear();
            m_chunksSeen = 0;
        }
        if (id != m_chunkTransitionId || part != m_chunksSeen) {
            m_socket.abort(); // out of order: start over with a fresh connection
            return;
        }
        m_chunkBuffer += message.value(QStringLiteral("chunk")).toString();
        ++m_chunksSeen;
        if (m_chunksSeen == total) {
            const QJsonObject whole = QJsonDocument::fromJson(m_chunkBuffer.toUtf8()).object();
            m_chunkBuffer.clear();
            m_chunkTransitionId.clear();
            m_chunksSeen = 0;
            handleTransition(whole);
        }
    } else if (type == QLatin1String("FatalError") || type == QLatin1String("AuthError")) {
        m_lastCloseReason = message.value(QStringLiteral("error")).toString(type);
        m_socket.abort();
    }
    // "Ping" only keeps the inactivity timer fresh, which the line above did.
}

void ConvexClient::handleTransition(const QJsonObject &message)
{
    for (const QJsonValue &entry : message.value(QStringLiteral("modifications")).toArray()) {
        const QJsonObject change = entry.toObject();
        const int id = change.value(QStringLiteral("queryId")).toInt(-1);
        if (m_queries.find(id) == m_queries.end()) {
            continue; // removed meanwhile
        }
        const QString kind = change.value(QStringLiteral("type")).toString();
        if (kind == QLatin1String("QueryUpdated")) {
            emit queryUpdated(id, change.value(QStringLiteral("value")));
        } else if (kind == QLatin1String("QueryFailed")) {
            emit queryFailed(id, change.value(QStringLiteral("errorMessage")).toString());
        }
    }
}

void ConvexClient::send(const QJsonObject &message)
{
    m_socket.sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
}

void ConvexClient::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}
