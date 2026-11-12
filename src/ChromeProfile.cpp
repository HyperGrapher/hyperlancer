#include "ChromeProfile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

namespace {

QString chromeExecutable()
{
#ifdef Q_OS_WIN
    const QStringList roots = {qEnvironmentVariable("ProgramFiles"),
                               qEnvironmentVariable("ProgramFiles(x86)"),
                               qEnvironmentVariable("LOCALAPPDATA")};
    for (const QString &root : roots) {
        if (root.isEmpty()) {
            continue;
        }
        const QString path = QDir(root).filePath(QStringLiteral("Google/Chrome/Application/chrome.exe"));
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return {};
#else
    for (const QString &name : {QStringLiteral("google-chrome"), QStringLiteral("google-chrome-stable")}) {
        const QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty()) {
            return path;
        }
    }
    return {};
#endif
}

QString localStatePath()
{
#ifdef Q_OS_WIN
    return QDir(qEnvironmentVariable("LOCALAPPDATA"))
        .filePath(QStringLiteral("Google/Chrome/User Data/Local State"));
#else
    return QDir::home().filePath(QStringLiteral(".config/google-chrome/Local State"));
#endif
}

} // namespace

QString ChromeProfile::directoryForName(const QByteArray &localStateJson, const QString &name)
{
    const QJsonObject cache = QJsonDocument::fromJson(localStateJson)
                                  .object()
                                  .value(QStringLiteral("profile"))
                                  .toObject()
                                  .value(QStringLiteral("info_cache"))
                                  .toObject();
    for (auto it = cache.begin(); it != cache.end(); ++it) {
        const QString profileName = it.value().toObject().value(QStringLiteral("name")).toString();
        if (profileName.compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
            return it.key();
        }
    }
    return {};
}

bool ChromeProfile::open(const QUrl &url, const QString &profileName)
{
    if (profileName.trimmed().isEmpty()) {
        return false;
    }
    const QString executable = chromeExecutable();
    QFile localState(localStatePath());
    if (executable.isEmpty() || !localState.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QString directory = directoryForName(localState.readAll(), profileName);
    if (directory.isEmpty()) {
        return false;
    }
    return QProcess::startDetached(
        executable, {QStringLiteral("--profile-directory=") + directory, url.toString()});
}
