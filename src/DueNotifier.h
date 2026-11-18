#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>

class AppSettings;
class Database;

// Watches the due dates the user set: website deadlines and to-dos. Every few minutes (and at
// start-up) it looks for items due today or overdue and raises each one once a day, so the
// app can keep running in the tray and still remind. What was shown is remembered in the
// settings, so a restart does not repeat a notice.
class DueNotifier : public QObject
{
    Q_OBJECT

public:
    DueNotifier(Database &database, AppSettings &settings, QObject *parent = nullptr);

    // Starts the checks; call once the database is ready.
    void start();
    void check();

signals:
    // Items not announced yet: { kind ("todo" or "deadline"), id, siteId, siteKey, title, dueOn,
    // overdue }.
    void dueItems(const QVariantList &items);

private:
    Database &m_database;
    AppSettings &m_settings;
    QTimer m_timer;
};
