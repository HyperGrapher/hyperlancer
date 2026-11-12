#include "MailStore.h"

#include "DbTime.h"
#include "PublicSuffixList.h"
#include "SqlExec.h"
#include "UrlNormalizer.h"

namespace MailStore {

void storeMessages(QSqlDatabase &db, qint64 accountId, qint64 siteId,
                   const QList<GmailQuery::MessageMeta> &messages)
{
    const QString sql =
        SqlExec::insertIgnore(db) +
        QStringLiteral(
            " INTO mail_messages (account_id, gmail_id, thread_id, site_id, direction, "
            "from_addr, to_addrs, subject, snippet, sent_at, has_attachments, is_bounce, "
            "is_auto_reply) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    for (const GmailQuery::MessageMeta &meta : messages) {
        SqlExec::run(db, sql,
                     {accountId, meta.gmailId, meta.threadId, siteId,
                      meta.sent ? QStringLiteral("sent") : QStringLiteral("received"),
                      meta.from.left(500), meta.to, meta.subject.left(1000), meta.snippet,
                      meta.sentAt.isValid() ? QVariant(DbTime::toDb(meta.sentAt)) : QVariant(),
                      meta.hasAttachments ? 1 : 0, meta.isBounce ? 1 : 0,
                      meta.isAutoReply ? 1 : 0});
    }
    SqlExec::run(db, QStringLiteral("UPDATE mail_accounts SET last_sync_at = ? WHERE id = ?"),
                 {DbTime::now(), accountId});
}

qint64 siteIdForAddress(QSqlDatabase &db, const QString &address)
{
    const int at = address.lastIndexOf(QLatin1Char('@'));
    if (at < 0) {
        return 0;
    }
    const QString domain = address.mid(at + 1).trimmed().toLower();
    if (domain.isEmpty() || GmailQuery::isFreeMailDomain(domain)) {
        return 0;
    }
    const UrlNormalizeResult normalized = normalizeUrl(domain, PublicSuffixList::bundled());
    if (normalized.value.registrableDomain.isEmpty()) {
        return 0;
    }
    QSqlQuery site = SqlExec::run(
        db,
        QStringLiteral("SELECT id FROM sites WHERE registrable_domain = ? AND archived_at IS NULL "
                       "ORDER BY id LIMIT 1"),
        {normalized.value.registrableDomain});
    return site.next() ? site.value(0).toLongLong() : 0;
}

} // namespace MailStore
