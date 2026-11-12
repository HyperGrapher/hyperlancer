#pragma once

#include <QObject>
#include <QString>
#include <QSystemTrayIcon>

class QMenu;

// The notification-area icon that keeps the app reachable while its window is hidden, and
// the desktop notifications for new mail. Needs QApplication (Qt Widgets).
class TrayIcon : public QObject
{
    Q_OBJECT

public:
    explicit TrayIcon(QObject *parent = nullptr);
    ~TrayIcon() override;

    [[nodiscard]] bool isAvailable() const { return m_available; }
    void notify(const QString &title, const QString &message);

signals:
    void showRequested();
    void quitRequested();
    // The user clicked a notification.
    void notificationClicked();

private:
    bool m_available = false;
    QSystemTrayIcon m_icon;
    QMenu *m_menu = nullptr;
};
