#pragma once

#include <QObject>
#include <QString>
#include <QSystemTrayIcon>

class QMenu;

// The notification-area icon that keeps the app reachable while its window is hidden.
// Needs QApplication (Qt Widgets).
class TrayIcon : public QObject
{
    Q_OBJECT

public:
    explicit TrayIcon(QObject *parent = nullptr);
    ~TrayIcon() override;

    [[nodiscard]] bool isAvailable() const { return m_available; }

signals:
    void showRequested();
    void quitRequested();

private:
    bool m_available = false;
    QSystemTrayIcon m_icon;
    QMenu *m_menu = nullptr;
};
