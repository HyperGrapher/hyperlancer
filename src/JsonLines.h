#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>

// Splits a child process's stdout into NDJSON events. Bytes arrive in arbitrary chunks, so
// an incomplete last line is kept until the next chunk. A line that is not a JSON object is
// returned in rejected, never thrown.
class JsonLines
{
public:
    struct Batch
    {
        QList<QJsonObject> events;
        QList<QByteArray> rejected;
    };

    // Longer lines are cut off and rejected, so a misbehaving tool cannot grow the buffer
    // without limit.
    static constexpr qsizetype kMaxLineBytes = 1024 * 1024;

    [[nodiscard]] Batch feed(const QByteArray &chunk);
    // Parses whatever is left after the process ended.
    [[nodiscard]] Batch finish();

private:
    void parseLine(const QByteArray &line, Batch &batch) const;

    QByteArray m_buffer;
};
