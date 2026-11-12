#include "JsonLines.h"

#include <QJsonDocument>
#include <QJsonParseError>

JsonLines::Batch JsonLines::feed(const QByteArray &chunk)
{
    Batch batch;
    m_buffer.append(chunk);
    qsizetype newline = m_buffer.indexOf('\n');
    while (newline >= 0) {
        parseLine(m_buffer.left(newline), batch);
        m_buffer.remove(0, newline + 1);
        newline = m_buffer.indexOf('\n');
    }
    if (m_buffer.size() > kMaxLineBytes) {
        batch.rejected.append(m_buffer.left(200) + "...");
        m_buffer.clear();
    }
    return batch;
}

JsonLines::Batch JsonLines::finish()
{
    Batch batch;
    parseLine(m_buffer, batch);
    m_buffer.clear();
    return batch;
}

void JsonLines::parseLine(const QByteArray &line, Batch &batch) const
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(trimmed, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        batch.rejected.append(trimmed.left(500));
        return;
    }
    batch.events.append(document.object());
}
