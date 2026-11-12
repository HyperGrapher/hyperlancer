#include "ToolPaths.h"

#include <QDir>
#include <QFileInfo>

namespace ToolPaths {

QString findScriptsDir(const QString &startDir)
{
    QDir dir(startDir);
    for (int depth = 0; depth < 6; ++depth) {
        const QString candidate = dir.filePath(QStringLiteral("web-dashboard/scripts"));
        if (QFileInfo::exists(QDir(candidate).filePath(QStringLiteral("pyproject.toml")))) {
            return QDir(candidate).absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

QString venvPython(const QString &scriptsDir)
{
    if (scriptsDir.isEmpty()) {
        return {};
    }
#ifdef Q_OS_WIN
    return QDir(scriptsDir).filePath(QStringLiteral(".venv/Scripts/python.exe"));
#else
    return QDir(scriptsDir).filePath(QStringLiteral(".venv/bin/python"));
#endif
}

} // namespace ToolPaths
