#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>

#include "GmailQuery.h"

// Mail metadata kept in the database (headers and snippet only, never bodies). Plain
// functions over a connection, run on the database thread.
namespace MailStore {

// Stores messages for a site; messages already stored are left alone.
void storeMessages(QSqlDatabase &db, qint64 accountId, qint64 siteId,
                   const QList<GmailQuery::MessageMeta> &messages);

// The tracked site whose registrable domain matches the address, or 0. Free-mail domains
// never match (PRD).
[[nodiscard]] qint64 siteIdForAddress(QSqlDatabase &db, const QString &address);

} // namespace MailStore
