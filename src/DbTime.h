#pragma once

#include <QDateTime>
#include <QString>
#include <QVariant>

// All times are stored in UTC as "yyyy-MM-dd HH:mm:ss.zzz". SQLite keeps the text (which sorts
// correctly), MySQL parses it into DATETIME(3). Reading accepts both forms.
namespace DbTime {

[[nodiscard]] QString toDb(const QDateTime &time);
[[nodiscard]] QString now();
// Invalid QDateTime for NULL or unparseable values.
[[nodiscard]] QDateTime fromDb(const QVariant &value);
// ISO 8601 with Z, for QML and JSON.
[[nodiscard]] QString toIso(const QVariant &value);

} // namespace DbTime
