#include "TrayIcon.h"

#include <QAction>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>

namespace {

// There is no icon file yet: a rounded accent tile with an H.
QIcon makeIcon()
{
    QPixmap pixmap(128, 128);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(QStringLiteral("#8E5BFF")));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(8, 8, 112, 112), 30, 30);
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(78);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QRectF(8, 8, 112, 112), Qt::AlignCenter, QStringLiteral("H"));
    return QIcon(pixmap);
}

} // namespace

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
    , m_available(QSystemTrayIcon::isSystemTrayAvailable())
{
    if (!m_available) {
        return;
    }
    const QIcon icon = makeIcon();
    QGuiApplication::setWindowIcon(icon);
    m_icon.setIcon(icon);
    m_icon.setToolTip(QStringLiteral("Hyperlancer"));

    m_menu = new QMenu();
    connect(m_menu->addAction(tr("Show Hyperlancer")), &QAction::triggered, this,
            &TrayIcon::showRequested);
    m_menu->addSeparator();
    connect(m_menu->addAction(tr("Quit")), &QAction::triggered, this, &TrayIcon::quitRequested);
    m_icon.setContextMenu(m_menu);

    connect(&m_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            emit showRequested();
        }
    });
    m_icon.show();
}

TrayIcon::~TrayIcon()
{
    delete m_menu; // the icon does not own its menu
}
