#include "SiteStore.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSqlRecord>

#include "DbTime.h"
#include "SiteOrganizer.h"
#include "SqlExec.h"

namespace SiteStore {

namespace {

QVariantMap recordToMap(const QSqlQuery &query, const QStringList &timeColumns = {})
{
    QVariantMap map;
    const QSqlRecord record = query.record();
    for (int i = 0; i < record.count(); ++i) {
        const QString name = record.fieldName(i);
        const QVariant value = query.value(i);
        if (timeColumns.contains(name)) {
            map.insert(name, DbTime::toIso(value));
        } else {
            map.insert(name, value.isNull() ? QVariant() : value);
        }
    }
    return map;
}

QVariantMap parseJsonObject(const QVariant &value)
{
    const QJsonDocument document = QJsonDocument::fromJson(value.toString().toUtf8());
    return document.isObject() ? document.object().toVariantMap() : QVariantMap();
}

// Comma separated tag names of the outer row "s", alphabetical. SQLite has no ordered
// GROUP_CONCAT, so it sorts in a derived table first.
QString tagNamesSubquery(const QSqlDatabase &db)
{
    if (SqlExec::isMySql(db)) {
        return QStringLiteral(
            "(SELECT GROUP_CONCAT(t.name ORDER BY t.name SEPARATOR ', ') FROM site_tags st JOIN "
            "tags t ON t.id = st.tag_id WHERE st.site_id = s.id)");
    }
    return QStringLiteral(
        "(SELECT GROUP_CONCAT(name, ', ') FROM (SELECT t.name AS name FROM site_tags st JOIN tags "
        "t ON t.id = st.tag_id WHERE st.site_id = s.id ORDER BY t.name))");
}

// ORDER BY expression for the list. Empty values always sort last, whatever the direction.
QString orderBy(const ListQuery &query)
{
    const QString direction = query.descending ? QStringLiteral("DESC") : QStringLiteral("ASC");
    const auto nullsLast = [&direction](const QString &column) {
        return QStringLiteral("(%1 IS NULL OR %1 = ''), %1 %2").arg(column, direction);
    };
    switch (query.sortKey) {
    case SortKey::Site:
        return QStringLiteral("s.site_key %1").arg(direction);
    case SortKey::Status: {
        // Pipeline order, not alphabetical.
        QString cases;
        const QStringList order = SiteOrganizer::statuses();
        for (int i = 0; i < order.size(); ++i) {
            cases += QStringLiteral(" WHEN '%1' THEN %2").arg(order.at(i)).arg(i);
        }
        return QStringLiteral("(CASE s.status%1 ELSE 0 END) %2").arg(cases, direction);
    }
    case SortKey::Category:
        return nullsLast(QStringLiteral("(SELECT c.name FROM categories c WHERE c.id = s.category_id)"));
    case SortKey::Deadline:
        return nullsLast(QStringLiteral("s.deadline_on"));
    case SortKey::Todos:
        return QStringLiteral("(SELECT COUNT(*) FROM todos td WHERE td.site_id = s.id AND "
                              "td.done_at IS NULL) %1")
            .arg(direction);
    case SortKey::Analyzed:
        break;
    }
    return QStringLiteral("COALESCE(s.last_fetched_at, s.created_at) %1").arg(direction);
}

QString likePattern(const QString &search)
{
    QString escaped = search.trimmed().toLower();
    escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escaped.replace(QStringLiteral("%"), QStringLiteral("\\%"));
    escaped.replace(QStringLiteral("_"), QStringLiteral("\\_"));
    return QStringLiteral("%") + escaped + QStringLiteral("%");
}

} // namespace

AddResult addSite(QSqlDatabase &db, const NormalizedUrl &url, const QString &source,
                  const QString &name)
{
    AddResult result;
    QSqlQuery existing = SqlExec::run(db, QStringLiteral("SELECT id FROM sites WHERE site_key = ?"),
                                      {url.siteKey}, &result.error);
    if (!result.error.isEmpty()) {
        return result;
    }
    if (existing.next()) {
        result.siteId = existing.value(0).toLongLong();
        return result;
    }
    const QString now = DbTime::now();
    QSqlQuery insert = SqlExec::run(
        db,
        QStringLiteral(
            "INSERT INTO sites (site_key, registrable_domain, url, name, source, status, "
            "created_at, updated_at) VALUES (?, ?, ?, ?, ?, 'none', ?, ?)"),
        {url.siteKey, SqlExec::textOrNull(url.registrableDomain), url.url,
         SqlExec::textOrNull(name.left(255)), source, now, now},
        &result.error);
    if (!result.error.isEmpty()) {
        return result;
    }
    result.siteId = insert.lastInsertId().toLongLong();
    result.created = true;
    logActivity(db, result.siteId, QStringLiteral("site.added"),
                source == QStringLiteral("discovery") ? QObject::tr("Added from discovery")
                                                      : QObject::tr("Site added"));
    return result;
}

ListResult listSites(QSqlDatabase &db, const ListQuery &listQuery)
{
    ListResult result;
    QString where = QStringLiteral("s.archived_at IS NULL");
    QVariantList binds;
    if (!listQuery.search.trimmed().isEmpty()) {
        where += QStringLiteral(
            " AND (LOWER(s.site_key) LIKE ? ESCAPE '\\' OR LOWER(COALESCE(s.title, '')) LIKE ? "
            "ESCAPE '\\' OR LOWER(COALESCE(s.cms, '')) LIKE ? ESCAPE '\\' OR "
            "LOWER(COALESCE(s.hosting, '')) LIKE ? ESCAPE '\\' OR EXISTS (SELECT 1 FROM "
            "categories c WHERE c.id = s.category_id AND LOWER(c.name) LIKE ? ESCAPE '\\') OR "
            "EXISTS (SELECT 1 FROM site_tags st JOIN tags t ON t.id = st.tag_id WHERE "
            "st.site_id = s.id AND LOWER(t.name) LIKE ? ESCAPE '\\'))");
        const QString pattern = likePattern(listQuery.search);
        binds << pattern << pattern << pattern << pattern << pattern << pattern;
    }
    if (!listQuery.category.isEmpty()) {
        where += QStringLiteral(
            " AND s.category_id = (SELECT c.id FROM categories c WHERE c.name = ?)");
        binds << listQuery.category;
    }
    if (!listQuery.status.isEmpty()) {
        where += QStringLiteral(" AND s.status = ?");
        binds << listQuery.status;
    }
    switch (listQuery.filter) {
    case Filter::All:
        break;
    case Filter::Failed:
        where += QStringLiteral(" AND s.last_fetch_outcome = 'failed'");
        break;
    case Filter::Expiring: {
        where +=
            QStringLiteral(" AND ((s.tls_valid_until IS NOT NULL AND s.tls_valid_until <= ?) OR "
                           "(s.domain_expires_on IS NOT NULL AND s.domain_expires_on <= ?))");
        const QString limit = QDate::currentDate().addDays(30).toString(Qt::ISODate);
        binds << limit << limit;
        break;
    }
    }

    // The newest unfinished profile job per site, so the list can show "analyzing".
    const QString sql =
        QStringLiteral(
            "SELECT s.id, s.site_key, s.url, s.title, s.cms, s.hosting, s.email_provider, "
            "s.http_status, "
            "s.last_fetch_outcome, s.last_fetched_at, s.tls_valid_until, s.domain_expires_on, "
            "s.source, s.favicon_url, s.status, s.deadline_on, "
            "(SELECT c.name FROM categories c WHERE c.id = s.category_id), "
            "%3, "
            "(SELECT COUNT(*) FROM todos td WHERE td.site_id = s.id AND td.done_at IS NULL), "
            "(SELECT j.status FROM jobs j WHERE j.site_id = s.id AND j.status IN "
            "('queued','running') "
            " ORDER BY j.id DESC LIMIT 1) AS active_job "
            "FROM sites s WHERE %1 "
            "ORDER BY %4, s.id DESC LIMIT %2")
            .arg(where)
            .arg(listQuery.limit)
            .arg(tagNamesSubquery(db))
            .arg(orderBy(listQuery));
    QSqlQuery query(db);
    query.setForwardOnly(true);
    if (!query.prepare(sql)) {
        result.error = query.lastError().text();
        return result;
    }
    for (const QVariant &bind : binds) {
        query.addBindValue(bind);
    }
    if (!query.exec()) {
        result.error = query.lastError().text();
        qWarning("listSites failed: %s", qPrintable(result.error));
        return result;
    }
    while (query.next()) {
        SiteRow row;
        row.id = query.value(0).toLongLong();
        row.siteKey = query.value(1).toString();
        row.url = query.value(2).toString();
        row.title = query.value(3).toString();
        row.cms = query.value(4).toString();
        row.hosting = query.value(5).toString();
        row.emailProvider = query.value(6).toString();
        row.httpStatus = query.value(7).toInt();
        row.outcome = query.value(8).toString();
        row.lastFetchedAt = DbTime::toIso(query.value(9));
        row.tlsValidUntil = query.value(10).toString();
        row.domainExpiresOn = query.value(11).toString();
        row.source = query.value(12).toString();
        row.faviconUrl = query.value(13).toString();
        row.status = query.value(14).toString();
        row.deadlineOn = query.value(15).toString();
        row.category = query.value(16).toString();
        row.tags = query.value(17).toString();
        row.openTodos = query.value(18).toInt();
        row.activeJobStatus = query.value(19).toString();
        result.rows.push_back(row);
    }

    QSqlQuery count =
        SqlExec::run(db, QStringLiteral("SELECT COUNT(*) FROM sites WHERE archived_at IS NULL"));
    if (count.next()) {
        result.total = count.value(0).toInt();
    }
    QSqlQuery categories = SqlExec::run(db, QStringLiteral("SELECT name FROM categories ORDER BY name"));
    while (categories.next()) {
        result.categories.append(categories.value(0).toString());
    }
    return result;
}

QVariantMap loadDetail(QSqlDatabase &db, qint64 siteId)
{
    QVariantMap detail;
    const QStringList siteTimes{QStringLiteral("last_fetched_at"), QStringLiteral("created_at"),
                                QStringLiteral("updated_at"), QStringLiteral("archived_at")};
    QSqlQuery site = SqlExec::run(db, QStringLiteral("SELECT * FROM sites WHERE id = ?"), {siteId});
    if (!site.next()) {
        detail.insert(QStringLiteral("error"), QObject::tr("This site no longer exists."));
        return detail;
    }
    const QVariantMap siteMap = recordToMap(site, siteTimes);
    detail.insert(QStringLiteral("site"), siteMap);

    const QVariant latestId = siteMap.value(QStringLiteral("latest_snapshot_id"));
    if (!latestId.isNull()) {
        detail.insert(QStringLiteral("snapshot"), loadSnapshot(db, latestId.toLongLong()));
    }

    QVariantList snapshots;
    QSqlQuery history = SqlExec::run(
        db,
        QStringLiteral("SELECT id, fetched_at, outcome, http_status, duration_ms, error_summary "
                       "FROM profile_snapshots WHERE site_id = ? ORDER BY fetched_at DESC, id DESC "
                       "LIMIT 50"),
        {siteId});
    while (history.next()) {
        snapshots.append(recordToMap(history, {QStringLiteral("fetched_at")}));
    }
    detail.insert(QStringLiteral("snapshots"), snapshots);

    QVariantList activity;
    QSqlQuery events = SqlExec::run(
        db,
        QStringLiteral("SELECT id, type, message, created_at FROM activity WHERE site_id = ? "
                       "ORDER BY id DESC LIMIT 60"),
        {siteId});
    while (events.next()) {
        activity.append(recordToMap(events, {QStringLiteral("created_at")}));
    }
    detail.insert(QStringLiteral("activity"), activity);

    for (const QString &strategy : {QStringLiteral("mobile"), QStringLiteral("desktop")}) {
        QSqlQuery speed = SqlExec::run(
            db,
            QStringLiteral("SELECT score, metrics_json, error, fetched_at FROM speed_checks WHERE "
                           "site_id = ? AND strategy = ? ORDER BY fetched_at DESC, id DESC LIMIT 1"),
            {siteId, strategy});
        if (speed.next()) {
            detail.insert(strategy == QStringLiteral("mobile") ? QStringLiteral("speed")
                                                               : QStringLiteral("speedDesktop"),
                          QVariantMap{{QStringLiteral("score"), speed.value(0)},
                                      {QStringLiteral("metrics"), parseJsonObject(speed.value(1))},
                                      {QStringLiteral("error"), speed.value(2)},
                                      {QStringLiteral("fetchedAt"), DbTime::toIso(speed.value(3))}});
        }
    }

    // Newest screenshot of each kind, as paths relative to the data folder.
    QVariantMap screenshots;
    QSqlQuery shots = SqlExec::run(
        db,
        QStringLiteral("SELECT kind, path, created_at FROM screenshots WHERE site_id = ? ORDER BY "
                       "id"),
        {siteId});
    while (shots.next()) {
        screenshots.insert(shots.value(0).toString(),
                           QVariantMap{{QStringLiteral("path"), shots.value(1)},
                                       {QStringLiteral("createdAt"), DbTime::toIso(shots.value(2))}});
    }
    detail.insert(QStringLiteral("screenshots"), screenshots);
    return detail;
}

QVariantMap loadSnapshot(QSqlDatabase &db, qint64 snapshotId)
{
    QSqlQuery query = SqlExec::run(
        db,
        QStringLiteral("SELECT id, fetched_at, outcome, http_status, duration_ms, error_summary, "
                       "tool_version, summary_json FROM profile_snapshots WHERE id = ?"),
        {snapshotId});
    if (!query.next()) {
        return {};
    }
    QVariantMap snapshot;
    snapshot.insert(QStringLiteral("id"), query.value(0));
    snapshot.insert(QStringLiteral("fetchedAt"), DbTime::toIso(query.value(1)));
    snapshot.insert(QStringLiteral("outcome"), query.value(2));
    snapshot.insert(QStringLiteral("httpStatus"), query.value(3));
    snapshot.insert(QStringLiteral("durationMs"), query.value(4));
    snapshot.insert(QStringLiteral("errorSummary"), query.value(5));
    snapshot.insert(QStringLiteral("toolVersion"), query.value(6));
    snapshot.insert(QStringLiteral("summary"), parseJsonObject(query.value(7)));
    return snapshot;
}

QSet<QString> knownDomains(QSqlDatabase &db, const QStringList &domains)
{
    QSet<QString> known;
    if (domains.isEmpty()) {
        return known;
    }
    QStringList marks;
    for (qsizetype i = 0; i < domains.size(); ++i) {
        marks.append(QStringLiteral("?"));
    }
    const QString placeholders = marks.join(QStringLiteral(", "));
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT registrable_domain, site_key FROM sites WHERE "
                                 "registrable_domain IN (%1) OR site_key IN (%1)")
                      .arg(placeholders));
    for (int pass = 0; pass < 2; ++pass) {
        for (const QString &domain : domains) {
            query.addBindValue(domain);
        }
    }
    if (!query.exec()) {
        qWarning("knownDomains failed: %s", qPrintable(query.lastError().text()));
        return known;
    }
    while (query.next()) {
        known.insert(query.value(0).toString());
        known.insert(query.value(1).toString());
    }
    known.remove(QString());
    return known;
}

QStringList siteDomains(QSqlDatabase &db, qint64 siteId)
{
    QStringList domains;
    QSqlQuery query = SqlExec::run(
        db, QStringLiteral("SELECT registrable_domain, site_key FROM sites WHERE id = ?"),
        {siteId});
    if (query.next()) {
        const QString registrable = query.value(0).toString();
        domains.append(registrable.isEmpty() ? query.value(1).toString() : registrable);
    }
    domains.removeAll(QString());
    return domains;
}

bool deleteSite(QSqlDatabase &db, qint64 siteId)
{
    QString error;
    SqlExec::run(db, QStringLiteral("DELETE FROM sites WHERE id = ?"), {siteId}, &error);
    return error.isEmpty();
}

bool setPreviewUrl(QSqlDatabase &db, qint64 siteId, const QString &url)
{
    QString error;
    SqlExec::run(db,
                 QStringLiteral("UPDATE sites SET preview_url = ?, updated_at = ? WHERE id = ?"),
                 {SqlExec::textOrNull(url.trimmed()), DbTime::now(), siteId}, &error);
    return error.isEmpty();
}

void saveSpeedCheck(QSqlDatabase &db, qint64 siteId, const QString &strategy, int score,
                    const QVariantMap &metrics, const QString &error)
{
    const QVariant scoreValue = score >= 0 ? QVariant(score) : SqlExec::nullInt();
    const QString metricsJson = QString::fromUtf8(
        QJsonDocument(QJsonObject::fromVariantMap(metrics)).toJson(QJsonDocument::Compact));
    const QString now = DbTime::now();
    SqlExec::run(db,
                 QStringLiteral("INSERT INTO speed_checks (site_id, strategy, score, metrics_json, "
                                "error, fetched_at) VALUES (?, ?, ?, ?, ?, ?)"),
                 {siteId, strategy, scoreValue, metricsJson, SqlExec::textOrNull(error), now});
    const bool isMobile = strategy == QStringLiteral("mobile");
    if (score >= 0) {
        if (isMobile) {
            SqlExec::run(db,
                         QStringLiteral("UPDATE sites SET mobile_score = ?, updated_at = ? WHERE id = ?"),
                         {score, now, siteId});
        }
        logActivity(db, siteId, QStringLiteral("speed.checked"),
                    (isMobile ? QObject::tr("Mobile performance score %1")
                              : QObject::tr("Desktop performance score %1"))
                        .arg(score));
    } else {
        logActivity(db, siteId, QStringLiteral("speed.failed"),
                    QObject::tr("Speed check (%1) failed: %2").arg(strategy, error.left(300)));
    }
}

bool saveScreenshot(QSqlDatabase &db, const QString &dataDir, qint64 siteId, const QString &kind,
                    const QByteArray &jpeg)
{
    if (jpeg.isEmpty()) {
        return false;
    }
    const QString folder = QStringLiteral("screenshots/%1").arg(siteId);
    const QString relativePath =
        QStringLiteral("%1/%2-%3.jpg").arg(folder, kind).arg(QDateTime::currentMSecsSinceEpoch());
    const QDir root(dataDir);
    root.mkpath(folder);
    QSaveFile file(root.filePath(relativePath));
    if (!file.open(QIODevice::WriteOnly) || file.write(jpeg) != jpeg.size() || !file.commit()) {
        qWarning("Could not write screenshot %s", qPrintable(relativePath));
        return false;
    }
    // Keep only the newest screenshot of each kind.
    QSqlQuery old = SqlExec::run(
        db, QStringLiteral("SELECT id, path FROM screenshots WHERE site_id = ? AND kind = ?"),
        {siteId, kind});
    while (old.next()) {
        QFile::remove(root.filePath(old.value(1).toString()));
    }
    SqlExec::run(db, QStringLiteral("DELETE FROM screenshots WHERE site_id = ? AND kind = ?"),
                 {siteId, kind});
    QString error;
    SqlExec::run(db,
                 QStringLiteral("INSERT INTO screenshots (site_id, kind, source, path, created_at) "
                                "VALUES (?, ?, 'pagespeed', ?, ?)"),
                 {siteId, kind, relativePath, DbTime::now()}, &error);
    return error.isEmpty();
}

QVariantMap dashboard(QSqlDatabase &db)
{
    QVariantMap result;
    const QString soon = QDate::currentDate().addDays(30).toString(Qt::ISODate);
    QSqlQuery counts = SqlExec::run(
        db,
        QStringLiteral(
            "SELECT COUNT(*), "
            "SUM(CASE WHEN last_fetched_at IS NOT NULL THEN 1 ELSE 0 END), "
            "SUM(CASE WHEN last_fetch_outcome = 'failed' THEN 1 ELSE 0 END), "
            "SUM(CASE WHEN (tls_valid_until IS NOT NULL AND tls_valid_until <= ?) OR "
            "(domain_expires_on IS NOT NULL AND domain_expires_on <= ?) THEN 1 ELSE 0 END) "
            "FROM sites WHERE archived_at IS NULL"),
        {soon, soon});
    if (counts.next()) {
        result.insert(QStringLiteral("sites"), counts.value(0).toInt());
        result.insert(QStringLiteral("analyzed"), counts.value(1).toInt());
        result.insert(QStringLiteral("failed"), counts.value(2).toInt());
        result.insert(QStringLiteral("expiring"), counts.value(3).toInt());
    }

    QVariantList recent;
    QSqlQuery recentQuery = SqlExec::run(
        db, QStringLiteral(
                "SELECT id, site_key, title, cms, last_fetch_outcome, last_fetched_at, "
                "mobile_score, favicon_url FROM sites WHERE last_fetched_at IS NOT NULL AND archived_at IS "
                "NULL ORDER BY last_fetched_at DESC LIMIT 7"));
    while (recentQuery.next()) {
        recent.append(recordToMap(recentQuery, {QStringLiteral("last_fetched_at")}));
    }
    result.insert(QStringLiteral("recent"), recent);

    QVariantList expiring;
    QSqlQuery expiringQuery = SqlExec::run(
        db,
        QStringLiteral(
            "SELECT id, site_key, tls_valid_until, domain_expires_on FROM sites WHERE "
            "archived_at IS NULL AND ((tls_valid_until IS NOT NULL AND tls_valid_until <= "
            "?) OR (domain_expires_on IS NOT NULL AND domain_expires_on <= ?)) ORDER BY "
            "LEAST_DATE LIMIT 6")
            .replace(QStringLiteral("LEAST_DATE"),
                     QStringLiteral("CASE WHEN tls_valid_until IS NULL THEN domain_expires_on WHEN "
                                    "domain_expires_on IS NULL THEN tls_valid_until WHEN "
                                    "tls_valid_until < domain_expires_on THEN tls_valid_until ELSE "
                                    "domain_expires_on END")),
        {soon, soon});
    while (expiringQuery.next()) {
        expiring.append(recordToMap(expiringQuery));
    }
    result.insert(QStringLiteral("expiringSites"), expiring);

    QVariantMap usage;
    QSqlQuery usageQuery =
        SqlExec::run(db, QStringLiteral("SELECT service, units FROM api_usage WHERE period = ?"),
                     {QDate::currentDate().toString(QStringLiteral("yyyy-MM"))});
    while (usageQuery.next()) {
        usage.insert(usageQuery.value(0).toString(), usageQuery.value(1).toInt());
    }
    result.insert(QStringLiteral("apiUsage"), usage);
    return result;
}

void addApiUsage(QSqlDatabase &db, const QString &service, int units)
{
    // Single writer per database, so update-then-insert is safe and portable.
    const QString period = QDate::currentDate().toString(QStringLiteral("yyyy-MM"));
    QSqlQuery update = SqlExec::run(
        db,
        QStringLiteral("UPDATE api_usage SET units = units + ? WHERE service = ? AND period = ?"),
        {units, service, period});
    if (update.numRowsAffected() <= 0) {
        SqlExec::run(
            db, QStringLiteral("INSERT INTO api_usage (service, period, units) VALUES (?, ?, ?)"),
            {service, period, units});
    }
}

void logActivity(QSqlDatabase &db, qint64 siteId, const QString &type, const QString &message,
                 const QVariantMap &data)
{
    const QVariant json =
        data.isEmpty()
            ? QVariant(QMetaType(QMetaType::QString))
            : QVariant(QString::fromUtf8(
                  QJsonDocument(QJsonObject::fromVariantMap(data)).toJson(QJsonDocument::Compact)));
    SqlExec::run(
        db,
        QStringLiteral("INSERT INTO activity (site_id, type, message, data_json, created_at) "
                       "VALUES (?, ?, ?, ?, ?)"),
        {siteId > 0 ? QVariant(siteId) : SqlExec::nullInt(), type, message, json, DbTime::now()});
}

} // namespace SiteStore
