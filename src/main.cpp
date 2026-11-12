#include <cstdlib>

#include <QCommandLineParser>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QUrl>

#include "AppContext.h"
#include "AppPaths.h"
#include "AppSettings.h"
#include "LogFile.h"
#include "TrayIcon.h"
#include "WindowChrome.h"

namespace {

struct LaunchOptions
{
    bool reducedMotion = false;
    bool translucencyAllowed = true;
    QString tourDirectory;
    QString settingsPath;
    QString dataDirectory;
};

LaunchOptions parseLaunchOptions(const QGuiApplication &app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Hyperlancer: freelance lead generation"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption reducedMotion(
        QStringLiteral("reduced-motion"),
        QStringLiteral("Start in reduced-motion mode for this session."));
    const QCommandLineOption noTranslucency(
        QStringLiteral("no-translucency"),
        QStringLiteral("Use an opaque window with square corners (for X11 without compositor)."));
    const QCommandLineOption tour(
        QStringLiteral("tour"),
        QStringLiteral("Run the automated tour, save screenshots to <dir>, then quit."),
        QStringLiteral("dir"));
    const QCommandLineOption settings(QStringLiteral("settings"),
                                      QStringLiteral("Use <file> to store settings."),
                                      QStringLiteral("file"));
    const QCommandLineOption dataDir(QStringLiteral("data-dir"),
                                     QStringLiteral("Use <dir> as the data folder."),
                                     QStringLiteral("dir"));
    parser.addOptions({reducedMotion, noTranslucency, tour, settings, dataDir});
    parser.process(app);

    LaunchOptions options;
    options.reducedMotion = parser.isSet(reducedMotion);
    options.translucencyAllowed = !parser.isSet(noTranslucency);
    options.tourDirectory = parser.value(tour);
    options.settingsPath = parser.value(settings);
    options.dataDirectory = parser.value(dataDir);
    if (options.settingsPath.isEmpty()) {
        const QString configDir =
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        options.settingsPath = QDir(configDir).filePath(QStringLiteral("settings.ini"));
    }
    if (options.dataDirectory.isEmpty()) {
        options.dataDirectory = AppPaths::defaultDataDir();
    }
    return options;
}

void loadBundledFonts()
{
    const QStringList fontFiles{
        QStringLiteral(":/fonts/Inter-Regular.ttf"),  QStringLiteral(":/fonts/Inter-Medium.ttf"),
        QStringLiteral(":/fonts/Inter-SemiBold.ttf"), QStringLiteral(":/fonts/Inter-Bold.ttf"),
        QStringLiteral(":/fonts/Phosphor.ttf"),       QStringLiteral(":/fonts/Phosphor-Fill.ttf"),
    };
    for (const QString &fontFile : fontFiles) {
        if (QFontDatabase::addApplicationFont(fontFile) < 0) {
            qWarning("Could not load bundled font %s", qPrintable(fontFile));
        }
    }

    QFont uiFont(QStringLiteral("Inter"));
    uiFont.setHintingPreference(QFont::PreferVerticalHinting);
    QGuiApplication::setFont(uiFont);
}

} // namespace

// "Ayşe Demir <ayse@firma.com>" becomes "Ayşe Demir"; a bare address stays as it is.
QString senderName(const QString &fromHeader)
{
    const int angle = fromHeader.indexOf(QLatin1Char('<'));
    const QString name = (angle > 0 ? fromHeader.left(angle) : fromHeader).trimmed();
    return name.remove(QLatin1Char('"'));
}

void notifyNewMail(TrayIcon &tray, const QVariantList &messages)
{
    const QVariantMap newest = messages.constFirst().toMap();
    if (messages.size() == 1) {
        tray.notify(QObject::tr("New email from %1").arg(senderName(newest.value(QStringLiteral("from")).toString())),
                    newest.value(QStringLiteral("subject")).toString());
        return;
    }
    tray.notify(QObject::tr("%1 new emails").arg(messages.size()),
                QObject::tr("Latest: %1").arg(newest.value(QStringLiteral("subject")).toString()));
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("HyperGrapher"));
    QGuiApplication::setApplicationName(QStringLiteral("Hyperlancer"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION_STRING));

    const LaunchOptions options = parseLaunchOptions(app);
    const AppPaths paths = AppPaths::create(options.dataDirectory);
    LogFile::install(paths.logsDir);
    qInfo("Hyperlancer %s starting, data folder %s", PROJECT_VERSION_STRING,
          qPrintable(QDir::toNativeSeparators(paths.dataDir)));
    loadBundledFonts();

    // Every control is custom-styled, so use the style designed for customization; native
    // styles would ignore or fight the custom backgrounds.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // The app keeps running in the tray when its window is closed. Without a tray (some Linux
    // desktops) closing the window quits, as usual.
    TrayIcon tray;
    QApplication::setQuitOnLastWindowClosed(!tray.isAvailable());

    WindowChrome chrome(options.translucencyAllowed);
    AppSettings settings(options.settingsPath);
    AppContext context(paths, settings);
    // Queued on the database thread before QML loads, so every query the pages send while
    // they build runs after the connection is open.
    context.start();

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("chrome"), QVariant::fromValue(&chrome)},
        {QStringLiteral("appSettings"), QVariant::fromValue(&settings)},
        {QStringLiteral("app"), QVariant::fromValue(&context)},
        {QStringLiteral("reducedMotionForced"), options.reducedMotion},
        {QStringLiteral("hasTray"), tray.isAvailable()},
        {QStringLiteral("tourDirectory"), options.tourDirectory},
    });
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Hyperlancer/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }

    QObject *root = engine.rootObjects().constFirst();
    if (auto *window = qobject_cast<QQuickWindow *>(root)) {
        chrome.attach(*window);
    }
    QObject::connect(&tray, &TrayIcon::showRequested, root,
                     [root]() { QMetaObject::invokeMethod(root, "showFromTray"); });
    QObject::connect(&tray, &TrayIcon::quitRequested, root,
                     [root]() { QMetaObject::invokeMethod(root, "requestQuit"); });
    QObject::connect(&tray, &TrayIcon::notificationClicked, root,
                     [root]() { QMetaObject::invokeMethod(root, "openEmails"); });
    QObject::connect(context.inbox(), &GmailInbox::newMessages, &tray,
                     [&tray, &settings](const QVariantList &messages) {
                         if (settings.notifyNewMail() && !messages.isEmpty()) {
                             notifyNewMail(tray, messages);
                         }
                     });

    return QApplication::exec();
}
