#include "VisitorStats.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>

#include "AppSettings.h"
#include "SecretStore.h"

namespace {

constexpr int kRows = 100;
// A visit this old is not news any more, even when we only just learned about it.
constexpr qint64 kNewVisitWindowMs = 30LL * 60 * 1000;

QString hintFor(const QString &key)
{
    return key.size() < 10 ? QStringLiteral("••••") : key.left(2) + QStringLiteral("••••••") + key.right(2);
}

bool isBotSession(const QVariantMap &session)
{
    return session.value(QStringLiteral("isBot")).toBool();
}

} // namespace

VisitorStats::VisitorStats(SecretStore &secrets, AppSettings &settings, QObject *parent)
    : QObject(parent)
    , m_secrets(secrets)
    , m_settings(settings)
    , m_nowMs(static_cast<double>(QDateTime::currentMSecsSinceEpoch()))
{
    m_clock.setInterval(15000);
    connect(&m_clock, &QTimer::timeout, this, [this]() {
        m_nowMs = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
        emit nowChanged();
        emit dataChanged(); // the live count depends on the time
    });
    m_clock.start();
    connect(&m_client, &ConvexClient::stateChanged, this, &VisitorStats::statusChanged);
    connect(&m_client, &ConvexClient::queryUpdated, this, &VisitorStats::onUpdated);
    connect(&m_client, &ConvexClient::queryFailed, this, &VisitorStats::onFailed);
}

QString VisitorStats::adminKey() const
{
    return m_secrets.read(SecretKeys::kConvexAdminKey).value_or(QString()).trimmed();
}

QUrl VisitorStats::deploymentUrl() const
{
    const QUrl url = QUrl::fromUserInput(m_settings.convexUrl());
    return url.isValid() && !url.host().isEmpty() ? url : QUrl();
}

bool VisitorStats::isConfigured() const
{
    return deploymentUrl().isValid() && hasKey();
}

bool VisitorStats::hasKey() const
{
    return !adminKey().isEmpty();
}

QString VisitorStats::keyHint() const
{
    return hintFor(adminKey());
}

QString VisitorStats::status() const
{
    if (!isConfigured()) {
        return QStringLiteral("notConfigured");
    }
    switch (m_client.state()) {
    case ConvexClient::State::Connected:
        return QStringLiteral("live");
    case ConvexClient::State::Connecting:
        return QStringLiteral("connecting");
    case ConvexClient::State::Disconnected:
        break;
    }
    return QStringLiteral("offline");
}

void VisitorStats::start()
{
    if (isConfigured()) {
        connectNow();
    }
}

void VisitorStats::connectNow()
{
    m_client.close();
    m_sessionsLoaded = false;
    m_visitorsLoaded = false;
    m_error.clear();
    const QJsonValue key(adminKey());
    // One subscription each for everyone: the switch for bots is a local filter on the page.
    m_sessionsQuery = m_client.subscribe(
        QStringLiteral("stats:recentSessions"),
        {{QStringLiteral("adminKey"), key}, {QStringLiteral("limit"), kRows}, {QStringLiteral("includeBots"), true}});
    m_visitorsQuery = m_client.subscribe(
        QStringLiteral("stats:recentVisitors"),
        {{QStringLiteral("adminKey"), key}, {QStringLiteral("limit"), kRows}, {QStringLiteral("includeBots"), true}});
    m_detailQuery = -1;
    m_eventsQuery = -1;
    if (!m_detailVisitorId.isEmpty()) {
        subscribeDetail();
    }
    m_client.open(deploymentUrl());
    emit statusChanged();
}

void VisitorStats::reconnect()
{
    if (isConfigured()) {
        connectNow();
    }
}

void VisitorStats::saveConnection(const QString &url, const QString &adminKeyValue)
{
    m_settings.setConvexUrl(url);
    const QString key = adminKeyValue.trimmed();
    if (!key.isEmpty()) {
        if (!m_secrets.write(SecretKeys::kConvexAdminKey, key)) {
            m_error = tr("The access key could not be saved.");
        }
    }
    emit connectionChanged();
    emit statusChanged();
    if (isConfigured()) {
        // A new address or key means every id seen so far may belong to another site.
        m_firstSessions = true;
        m_notified.clear();
        connectNow();
    } else {
        m_client.close();
    }
}

void VisitorStats::clearKey()
{
    m_secrets.remove(SecretKeys::kConvexAdminKey);
    m_client.close();
    m_allSessions.clear();
    m_allVisitors.clear();
    m_sessionsLoaded = false;
    m_visitorsLoaded = false;
    emit connectionChanged();
    emit statusChanged();
    emit dataChanged();
}

void VisitorStats::setShowBots(bool show)
{
    if (m_showBots == show) {
        return;
    }
    m_showBots = show;
    emit dataChanged();
}

QVariantList VisitorStats::sessions() const
{
    if (m_showBots) {
        return m_allSessions;
    }
    QVariantList people;
    for (const QVariant &session : m_allSessions) {
        if (!isBotSession(session.toMap())) {
            people.append(session);
        }
    }
    return people;
}

QVariantList VisitorStats::visitors() const
{
    if (m_showBots) {
        return m_allVisitors;
    }
    QVariantList people;
    for (const QVariant &row : m_allVisitors) {
        if (!isBotSession(row.toMap().value(QStringLiteral("latest")).toMap())) {
            people.append(row);
        }
    }
    return people;
}

int VisitorStats::botSessionCount() const
{
    int count = 0;
    for (const QVariant &session : m_allSessions) {
        count += isBotSession(session.toMap()) ? 1 : 0;
    }
    return count;
}

int VisitorStats::liveCount() const
{
    // The site's tracker sends a heartbeat about every minute while a page is open.
    constexpr double kLiveMs = 75000;
    int count = 0;
    for (const QVariant &entry : m_allSessions) {
        const QVariantMap session = entry.toMap();
        if (!isBotSession(session) && m_nowMs - session.value(QStringLiteral("lastActivityAt")).toDouble() < kLiveMs) {
            ++count;
        }
    }
    return count;
}

void VisitorStats::onUpdated(int queryId, const QJsonValue &value)
{
    m_error.clear();
    if (queryId == m_sessionsQuery) {
        applySessions(value.toArray());
        m_sessionsLoaded = true;
    } else if (queryId == m_visitorsQuery) {
        m_allVisitors.clear();
        for (const QJsonValue &row : value.toArray()) {
            m_allVisitors.append(row.toObject().toVariantMap());
        }
        m_visitorsLoaded = true;
    } else if (queryId == m_detailQuery) {
        m_detail = value.toObject().toVariantMap();
        emit detailChanged();
        return;
    } else if (queryId == m_eventsQuery) {
        // The backend answers { events, capped }.
        m_detailEvents = value.toObject().value(QStringLiteral("events")).toArray().toVariantList();
        m_detailEventsCapped = value.toObject().value(QStringLiteral("capped")).toBool();
        emit detailChanged();
        return;
    }
    emit statusChanged();
    emit dataChanged();
}

void VisitorStats::onFailed(int, const QString &message)
{
    // The server's text for a wrong key is long and technical.
    m_error = message.contains(QLatin1String("nauthorized"), Qt::CaseInsensitive) || message.contains(QLatin1String("admin"), Qt::CaseInsensitive)
                  ? tr("The access key was refused.")
                  : message.left(200);
    emit statusChanged();
}

void VisitorStats::applySessions(const QJsonArray &rows)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_allSessions.clear();
    QVariantList fresh;
    for (const QJsonValue &row : rows) {
        const QVariantMap session = row.toObject().toVariantMap();
        m_allSessions.append(session);
        const QString id = session.value(QStringLiteral("_id")).toString();
        if (isBotSession(session) || m_notified.contains(id)) {
            continue;
        }
        // A session that scored as a bot at first and was cleared later counts when it is cleared.
        m_notified.insert(id);
        const bool recent = now - static_cast<qint64>(session.value(QStringLiteral("startedAt")).toDouble()) < kNewVisitWindowMs;
        if (!m_firstSessions && recent) {
            fresh.append(session);
        }
    }
    m_firstSessions = false;
    if (m_settings.notifyVisitors()) {
        for (const QVariant &session : fresh) {
            emit newVisitor(session.toMap());
        }
    }
}

void VisitorStats::subscribeDetail()
{
    const QJsonValue key(adminKey());
    m_detailQuery = m_client.subscribe(
        QStringLiteral("stats:visitorDetail"),
        {{QStringLiteral("adminKey"), key}, {QStringLiteral("visitorId"), m_detailVisitorId}});
    if (!m_detailSessionId.isEmpty()) {
        m_eventsQuery = m_client.subscribe(
            QStringLiteral("stats:sessionEvents"),
            {{QStringLiteral("adminKey"), key}, {QStringLiteral("sessionId"), m_detailSessionId}});
    }
}

void VisitorStats::unsubscribeDetail()
{
    if (m_detailQuery >= 0) {
        m_client.unsubscribe(m_detailQuery);
    }
    if (m_eventsQuery >= 0) {
        m_client.unsubscribe(m_eventsQuery);
    }
    m_detailQuery = -1;
    m_eventsQuery = -1;
}

void VisitorStats::openVisitor(const QString &visitorId, const QString &sessionId)
{
    unsubscribeDetail();
    m_detailVisitorId = visitorId;
    m_detailSessionId = sessionId;
    m_detail.clear();
    m_detailEvents.clear();
    m_detailEventsCapped = false;
    subscribeDetail();
    emit detailChanged();
}

void VisitorStats::selectSession(const QString &sessionId)
{
    if (sessionId == m_detailSessionId) {
        return;
    }
    if (m_eventsQuery >= 0) {
        m_client.unsubscribe(m_eventsQuery);
        m_eventsQuery = -1;
    }
    m_detailSessionId = sessionId;
    m_detailEvents.clear();
    m_detailEventsCapped = false;
    if (!sessionId.isEmpty() && m_detailQuery >= 0) {
        m_eventsQuery = m_client.subscribe(
            QStringLiteral("stats:sessionEvents"),
            {{QStringLiteral("adminKey"), QJsonValue(adminKey())}, {QStringLiteral("sessionId"), sessionId}});
    }
    emit detailChanged();
}

void VisitorStats::closeVisitor()
{
    unsubscribeDetail();
    m_detailVisitorId.clear();
    m_detailSessionId.clear();
    m_detail.clear();
    m_detailEvents.clear();
    m_detailEventsCapped = false;
    emit detailChanged();
}
