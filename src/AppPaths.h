#pragma once

#include <QString>

// Folders inside the local data folder. Job specs, logs and the SQLite file live here; the
// database stores only paths relative to dataDir, so the folder can be moved as a whole.
struct AppPaths
{
    QString dataDir;
    QString jobsDir;
    QString logsDir;
    QString screenshotsDir;
    QString sqlitePath;

    // Builds the layout under dataDir and creates the folders that are missing.
    [[nodiscard]] static AppPaths create(const QString &dataDir);
    // The default data folder: QStandardPaths::AppDataLocation.
    [[nodiscard]] static QString defaultDataDir();

    [[nodiscard]] QString jobDir(qint64 jobId) const;
    // A path stored in the database (relative to dataDir) as an absolute path.
    [[nodiscard]] QString absolutePath(const QString &relativePath) const;
    [[nodiscard]] bool isWritable() const;
};
