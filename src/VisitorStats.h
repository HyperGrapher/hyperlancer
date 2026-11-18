#pragma once

#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "ConvexClient.h"

class AppSettings;
class SecretStore;

// Live visitor data of the portfolio site, read from its Convex backend (the same queries the
// site's own dashboard uses). It subscribes once, including bots, so the bot switch on the page
// is only a local filter and a notification can be raised even when the page is not open.
class VisitorStats : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("VisitorStats is created by the application.")

    Q_PROPERTY(bool configured READ isConfigured NOTIFY connectionChanged)
    Q_PROPERTY(bool hasKey READ hasKey NOTIFY connectionChanged)
    Q_PROPERTY(QString keyHint READ keyHint NOTIFY connectionChanged)
    // "notConfigured", "connecting", "live" or "offline".
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    Q_PROPERTY(bool showBots READ showBots WRITE setShowBots NOTIFY dataChanged)
    // Milliseconds since the epoch, refreshed every 15 seconds so "live" and "x min ago" stay right.
    Q_PROPERTY(double nowMs READ nowMs NOTIFY nowChanged)
    // Sessions, newest first (a Convex "sessions" document each); bots hidden unless showBots.
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY dataChanged)
    // { visitor, latest } per visitor, last seen first.
    Q_PROPERTY(QVariantList visitors READ visitors NOTIFY dataChanged)
    Q_PROPERTY(int botSessionCount READ botSessionCount NOTIFY dataChanged)
    Q_PROPERTY(int liveCount READ liveCount NOTIFY dataChanged)
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY dataChanged)

    // The visitor page: { visitor, sessions } of visitorDetail, the events of the chosen session.
    Q_PROPERTY(QString detailVisitorId READ detailVisitorId NOTIFY detailChanged)
    Q_PROPERTY(QString detailSessionId READ detailSessionId NOTIFY detailChanged)
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)
    Q_PROPERTY(QVariantList detailEvents READ detailEvents NOTIFY detailChanged)
    // True when the session has more events than the backend sends.
    Q_PROPERTY(bool detailEventsCapped READ detailEventsCapped NOTIFY detailChanged)

public:
    VisitorStats(SecretStore &secrets, AppSettings &settings, QObject *parent = nullptr);

    [[nodiscard]] bool isConfigured() const;
    [[nodiscard]] bool hasKey() const;
    [[nodiscard]] QString keyHint() const;
    [[nodiscard]] QString status() const;
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] bool showBots() const { return m_showBots; }
    void setShowBots(bool show);
    [[nodiscard]] double nowMs() const { return m_nowMs; }
    [[nodiscard]] QVariantList sessions() const;
    [[nodiscard]] QVariantList visitors() const;
    [[nodiscard]] int botSessionCount() const;
    [[nodiscard]] int liveCount() const;
    [[nodiscard]] bool isLoaded() const { return m_sessionsLoaded && m_visitorsLoaded; }
    [[nodiscard]] QString detailVisitorId() const { return m_detailVisitorId; }
    [[nodiscard]] QString detailSessionId() const { return m_detailSessionId; }
    [[nodiscard]] QVariantMap detail() const { return m_detail; }
    [[nodiscard]] QVariantList detailEvents() const { return m_detailEvents; }
    [[nodiscard]] bool detailEventsCapped() const { return m_detailEventsCapped; }

    // Saves the address and (when not empty) the access key, then reconnects.
    Q_INVOKABLE void saveConnection(const QString &url, const QString &adminKey);
    Q_INVOKABLE void clearKey();
    Q_INVOKABLE void reconnect();

    Q_INVOKABLE void openVisitor(const QString &visitorId, const QString &sessionId = QString());
    Q_INVOKABLE void selectSession(const QString &sessionId);
    Q_INVOKABLE void closeVisitor();

    // Starts the connection when an address and key are saved. Call once the app is up.
    void start();

signals:
    void connectionChanged();
    void statusChanged();
    void dataChanged();
    void nowChanged();
    void detailChanged();
    // A session of a person (not a bot) that was not seen before. Not raised for the first data
    // after the app starts, so old visits do not all ring at once.
    void newVisitor(const QVariantMap &session);

private:
    void connectNow();
    void onUpdated(int queryId, const QJsonValue &value);
    void onFailed(int queryId, const QString &message);
    void applySessions(const QJsonArray &rows);
    void subscribeDetail();
    void unsubscribeDetail();
    [[nodiscard]] QString adminKey() const;
    [[nodiscard]] QUrl deploymentUrl() const;

    SecretStore &m_secrets;
    AppSettings &m_settings;
    ConvexClient m_client;
    QTimer m_clock;

    int m_sessionsQuery = -1;
    int m_visitorsQuery = -1;
    int m_detailQuery = -1;
    int m_eventsQuery = -1;
    bool m_sessionsLoaded = false;
    bool m_visitorsLoaded = false;
    bool m_firstSessions = true;
    QSet<QString> m_notified;

    bool m_showBots = false;
    double m_nowMs = 0;
    QString m_error;
    QVariantList m_allSessions;
    QVariantList m_allVisitors;
    QString m_detailVisitorId;
    QString m_detailSessionId;
    QVariantMap m_detail;
    QVariantList m_detailEvents;
    bool m_detailEventsCapped = false;
};
