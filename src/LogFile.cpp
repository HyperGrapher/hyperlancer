#include "LogFile.h"

#include <cstdio>
#include <memory>

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRandomGenerator>

namespace {

// The message handler is a plain function, so its state has to live at namespace scope. It
// is written only under the mutex.
struct LogState
{
    QMutex mutex;
    QString logsDir;
    QDate openDate;
    std::unique_ptr<QFile> file;
};

LogState &state()
{
    static LogState instance;
    return instance;
}

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "debug";
    case QtInfoMsg:
        return "info";
    case QtWarningMsg:
        return "warn";
    case QtCriticalMsg:
        return "error";
    case QtFatalMsg:
        return "fatal";
    }
    return "log";
}

void pruneOldLogs(const QString &logsDir)
{
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-LogFile::kKeepDays);
    const QFileInfoList files =
        QDir(logsDir).entryInfoList({QStringLiteral("hyperlancer-*.log")}, QDir::Files);
    for (const QFileInfo &info : files) {
        if (info.lastModified() < cutoff) {
            QFile::remove(info.absoluteFilePath());
        }
    }
}

void handleMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QString line = QStringLiteral("%1 [%2] %3%4\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  QLatin1String(levelName(type)),
                                  context.category && qstrcmp(context.category, "default") != 0
                                      ? QLatin1String(context.category) + QStringLiteral(": ")
                                      : QString(),
                                  message);
    const QByteArray bytes = line.toUtf8();
    std::fputs(bytes.constData(), stderr);

    LogState &log = state();
    QMutexLocker lock(&log.mutex);
    const QDate today = QDate::currentDate();
    if (!log.file || log.openDate != today) {
        log.file = std::make_unique<QFile>(
            QDir(log.logsDir)
                .filePath(QStringLiteral("hyperlancer-%1.log").arg(today.toString(Qt::ISODate))));
        if (!log.file->open(QIODevice::Append | QIODevice::Text)) {
            log.file.reset();
            return;
        }
        log.openDate = today;
    }
    log.file->write(bytes);
    log.file->flush();
}

} // namespace

namespace LogFile {

void install(const QString &logsDir)
{
    {
        QMutexLocker lock(&state().mutex);
        state().logsDir = logsDir;
    }
    pruneOldLogs(logsDir);
    qInstallMessageHandler(handleMessage);
}

QString newReference()
{
    return QString::number(QRandomGenerator::global()->bounded(0x100000, 0xFFFFFF), 16).toUpper();
}

} // namespace LogFile
