#include "AppPaths.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

AppPaths AppPaths::create(const QString &dataDir)
{
    const QDir root(dataDir);
    AppPaths paths;
    paths.dataDir = root.absolutePath();
    paths.jobsDir = root.filePath(QStringLiteral("jobs"));
    paths.logsDir = root.filePath(QStringLiteral("logs"));
    paths.screenshotsDir = root.filePath(QStringLiteral("screenshots"));
    paths.sqlitePath = root.filePath(QStringLiteral("hyperlancer.sqlite"));
    for (const QString &folder : {paths.dataDir, paths.jobsDir, paths.logsDir, paths.screenshotsDir}) {
        QDir().mkpath(folder);
    }
    return paths;
}

QString AppPaths::defaultDataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString AppPaths::jobDir(qint64 jobId) const
{
    return QDir(jobsDir).filePath(QString::number(jobId));
}

QString AppPaths::absolutePath(const QString &relativePath) const
{
    return QDir(dataDir).filePath(relativePath);
}

bool AppPaths::isWritable() const
{
    QFile probe(QDir(dataDir).filePath(QStringLiteral(".write-test")));
    if (!probe.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    probe.close();
    return probe.remove();
}
