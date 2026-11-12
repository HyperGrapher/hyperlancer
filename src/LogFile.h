#pragma once

#include <QString>

// Sends Qt's log output (qDebug/qWarning/...) to a daily file in the logs folder as well as
// stderr. Files older than kKeepDays are deleted at start-up.
namespace LogFile {

inline constexpr int kKeepDays = 14;

void install(const QString &logsDir);
// Short random reference written next to an error, so a message shown to the user can be
// found in the log ("ref 4F2A9C").
[[nodiscard]] QString newReference();

} // namespace LogFile
