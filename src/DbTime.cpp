#include "DbTime.h"

#include <QTimeZone>

namespace {

constexpr auto kDbFormat = "yyyy-MM-dd HH:mm:ss.zzz";

} // namespace

namespace DbTime {

QString toDb(const QDateTime &time)
{
    return time.toUTC().toString(QLatin1String(kDbFormat));
}

QString now()
{
    return toDb(QDateTime::currentDateTimeUtc());
}

QDateTime fromDb(const QVariant &value)
{
    if (value.isNull()) {
        return {};
    }
    if (value.typeId() == QMetaType::QDateTime) {
        // The MySQL driver returns the stored wall time without a zone; it is UTC.
        const QDateTime raw = value.toDateTime();
        return QDateTime(raw.date(), raw.time(), QTimeZone::UTC);
    }
    const QString text = value.toString().trimmed();
    QDateTime parsed = QDateTime::fromString(text, QLatin1String(kDbFormat));
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
    }
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(text, Qt::ISODate);
    }
    if (!parsed.isValid()) {
        return {};
    }
    return QDateTime(parsed.date(), parsed.time(), QTimeZone::UTC);
}

QString toIso(const QVariant &value)
{
    const QDateTime time = fromDb(value);
    return time.isValid() ? time.toString(Qt::ISODateWithMs) : QString();
}

} // namespace DbTime
