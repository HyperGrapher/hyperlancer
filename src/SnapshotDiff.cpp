#include "SnapshotDiff.h"

#include <initializer_list>
#include <map>

#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>

namespace SnapshotDiff {

namespace {

// summary["a"]["b"] as a QVariant; null when any step is missing.
QVariant at(const QVariantMap &root, std::initializer_list<const char *> path)
{
    QVariant current = root;
    for (const char *key : path) {
        if (current.typeId() != QMetaType::QVariantMap) {
            return {};
        }
        current = current.toMap().value(QLatin1String(key));
    }
    return current;
}

// A value as text for the user; empty when there is nothing to show.
QString show(const QVariant &value)
{
    if (value.isNull() || !value.isValid()) {
        return {};
    }
    if (value.typeId() == QMetaType::Bool) {
        return value.toBool() ? QObject::tr("yes") : QObject::tr("no");
    }
    if (value.typeId() == QMetaType::Double) {
        return QString::number(value.toDouble(), 'g', 15);
    }
    return value.toString();
}

QString day(const QVariant &value)
{
    return show(value).left(10);
}

QStringList strings(const QVariant &list)
{
    QStringList result;
    for (const QVariant &item : list.toList()) {
        const QString text = show(item);
        if (!text.isEmpty() && !result.contains(text)) {
            result.append(text);
        }
    }
    return result;
}

// Values of one key from a list of maps (for example the host of each mail server).
QStringList column(const QVariant &list, const char *key)
{
    QVariantList values;
    for (const QVariant &item : list.toList()) {
        values.append(item.toMap().value(QLatin1String(key)));
    }
    return strings(values);
}

QVariantMap change(const QString &path, const QString &label, const QString &kind,
                   const QString &before, const QString &after)
{
    return {{QStringLiteral("path"), path},
            {QStringLiteral("label"), label},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("before"), before},
            {QStringLiteral("after"), after}};
}

void scalar(QVariantList &out, const QString &path, const QString &label, const QString &before,
            const QString &after)
{
    if (before == after) {
        return;
    }
    const QString kind = before.isEmpty() ? QStringLiteral("added")
                         : after.isEmpty() ? QStringLiteral("removed")
                                           : QStringLiteral("changed");
    out.append(change(path, label, kind, before, after));
}

void scalar(QVariantList &out, const QString &path, const QString &label, const QVariant &before,
            const QVariant &after)
{
    scalar(out, path, label, show(before), show(after));
}

void set(QVariantList &out, const QString &path, const QString &label, const QStringList &before,
         const QStringList &after)
{
    for (const QString &item : before) {
        if (!after.contains(item)) {
            out.append(change(path, label, QStringLiteral("removed"), item, {}));
        }
    }
    for (const QString &item : after) {
        if (!before.contains(item)) {
            out.append(change(path, label, QStringLiteral("added"), {}, item));
        }
    }
}

// Technologies match on category and name; a different version is "changed".
void technologies(QVariantList &out, const QVariantList &before, const QVariantList &after)
{
    const auto index = [](const QVariantList &list) {
        std::map<QString, QVariantMap> map;
        for (const QVariant &item : list) {
            const QVariantMap tech = item.toMap();
            map[tech.value(QStringLiteral("category")).toString() + QLatin1Char('|') +
                tech.value(QStringLiteral("name")).toString()] = tech;
        }
        return map;
    };
    const auto label = [](const QVariantMap &tech) {
        return (tech.value(QStringLiteral("name")).toString() + QLatin1Char(' ') +
                show(tech.value(QStringLiteral("version"))))
            .trimmed();
    };
    const auto older = index(before);
    const auto newer = index(after);
    for (const auto &[key, tech] : older) {
        if (!newer.contains(key)) {
            out.append(change(QStringLiteral("tech.") + key, tech.value(QStringLiteral("category")).toString(),
                              QStringLiteral("removed"), label(tech), {}));
        }
    }
    for (const auto &[key, tech] : newer) {
        const auto previous = older.find(key);
        if (previous == older.end()) {
            out.append(change(QStringLiteral("tech.") + key, tech.value(QStringLiteral("category")).toString(),
                              QStringLiteral("added"), {}, label(tech)));
        } else if (show(previous->second.value(QStringLiteral("version"))) !=
                   show(tech.value(QStringLiteral("version")))) {
            scalar(out, QStringLiteral("tech.") + key,
                   tech.value(QStringLiteral("category")).toString() + QStringLiteral(" · ") +
                       tech.value(QStringLiteral("name")).toString(),
                   show(previous->second.value(QStringLiteral("version"))),
                   show(tech.value(QStringLiteral("version"))));
        }
    }
}

} // namespace

QVariantMap diff(const QVariantMap &before, const QVariantMap &after)
{
    // One lambda per kind keeps each section below to a list of "this field, this label".
    const auto sc = [&](QVariantList &out, std::initializer_list<const char *> path, const QString &label,
                        bool asDay = false) {
        QString dotted;
        for (const char *key : path) {
            dotted += (dotted.isEmpty() ? QString() : QStringLiteral(".")) + QLatin1String(key);
        }
        const QVariant a = at(before, path);
        const QVariant b = at(after, path);
        if (asDay) {
            scalar(out, dotted, label, day(a), day(b));
        } else {
            scalar(out, dotted, label, a, b);
        }
    };
    const auto list = [&](QVariantList &out, std::initializer_list<const char *> path, const QString &label) {
        QString dotted;
        for (const char *key : path) {
            dotted += (dotted.isEmpty() ? QString() : QStringLiteral(".")) + QLatin1String(key);
        }
        set(out, dotted, label, strings(at(before, path)), strings(at(after, path)));
    };

    struct Section
    {
        QString name;
        QVariantList changes;
    };
    std::vector<Section> sections;

    Section tech{QObject::tr("Technologies"), {}};
    technologies(tech.changes, at(before, {"tech"}).toList(), at(after, {"tech"}).toList());
    sections.push_back(tech);

    Section hosting{QObject::tr("Hosting"), {}};
    list(hosting.changes, {"hosting", "ips"}, QObject::tr("IP address"));
    list(hosting.changes, {"hosting", "ipv6"}, QObject::tr("IPv6 address"));
    sc(hosting.changes, {"hosting", "org"}, QObject::tr("Hosting network"));
    sc(hosting.changes, {"hosting", "edge"}, QObject::tr("Edge / CDN"));
    sc(hosting.changes, {"hosting", "server"}, QObject::tr("Web server"));
    sc(hosting.changes, {"hosting", "language"}, QObject::tr("Backend language"));
    sc(hosting.changes, {"hosting", "languageVersion"}, QObject::tr("Language version"));
    sc(hosting.changes, {"cms", "name"}, QObject::tr("CMS"));
    sc(hosting.changes, {"cms", "version"}, QObject::tr("CMS version"));
    sc(hosting.changes, {"cms", "theme"}, QObject::tr("WordPress theme"));
    list(hosting.changes, {"cms", "plugins"}, QObject::tr("WordPress plugin"));
    sections.push_back(hosting);

    Section dns{QObject::tr("DNS and email"), {}};
    list(dns.changes, {"dns", "ns"}, QObject::tr("Nameserver"));
    set(dns.changes, QStringLiteral("dns.mx"), QObject::tr("Mail server"),
        column(at(before, {"dns", "mx"}), "host"), column(at(after, {"dns", "mx"}), "host"));
    sc(dns.changes, {"dns", "emailProvider"}, QObject::tr("Email provider"));
    sc(dns.changes, {"dns", "spf"}, QObject::tr("SPF"));
    sc(dns.changes, {"dns", "dmarc"}, QObject::tr("DMARC"));
    sc(dns.changes, {"dns", "dnssec"}, QObject::tr("DNSSEC"));
    list(dns.changes, {"dns", "caa"}, QObject::tr("CAA record"));
    sections.push_back(dns);

    Section tls{QObject::tr("SSL / TLS"), {}};
    sc(tls.changes, {"tls", "issuer"}, QObject::tr("Issuer"));
    sc(tls.changes, {"tls", "validUntil"}, QObject::tr("Valid until"), true);
    sc(tls.changes, {"tls", "verified"}, QObject::tr("Certificate trusted"));
    sections.push_back(tls);

    Section domain{QObject::tr("Domain"), {}};
    sc(domain.changes, {"domain", "registrar"}, QObject::tr("Registrar"));
    sc(domain.changes, {"domain", "registrant"}, QObject::tr("Registrant"));
    sc(domain.changes, {"domain", "expires"}, QObject::tr("Expires"), true);
    sc(domain.changes, {"domain", "transferLocked"}, QObject::tr("Transfer lock"));
    list(domain.changes, {"domain", "statuses"}, QObject::tr("Domain status"));
    sections.push_back(domain);

    Section seo{QObject::tr("SEO"), {}};
    sc(seo.changes, {"seo", "title"}, QObject::tr("Title"));
    sc(seo.changes, {"seo", "description"}, QObject::tr("Meta description"));
    sc(seo.changes, {"seo", "canonical"}, QObject::tr("Canonical URL"));
    sc(seo.changes, {"seo", "robots"}, QObject::tr("Meta robots"));
    sc(seo.changes, {"seo", "lang"}, QObject::tr("Language"));
    sc(seo.changes, {"seo", "h1Count"}, QObject::tr("H1 count"));
    list(seo.changes, {"seo", "structuredData"}, QObject::tr("Structured data type"));
    sections.push_back(seo);

    Section http{QObject::tr("HTTP"), {}};
    sc(http.changes, {"http", "status"}, QObject::tr("HTTP status"));
    sc(http.changes, {"url", "final"}, QObject::tr("Final URL"));
    sc(http.changes, {"http", "compression"}, QObject::tr("Compression"));
    sc(http.changes, {"http", "httpsRedirect"}, QObject::tr("HTTP to HTTPS redirect"));
    sc(http.changes, {"http", "wwwCanonical"}, QObject::tr("Canonical host"));
    sections.push_back(http);

    Section contact{QObject::tr("Contact"), {}};
    list(contact.changes, {"contact", "emails"}, QObject::tr("Email"));
    list(contact.changes, {"contact", "phones"}, QObject::tr("Phone"));
    set(contact.changes, QStringLiteral("contact.socials"), QObject::tr("Social profile"),
        column(at(before, {"contact", "socials"}), "url"), column(at(after, {"contact", "socials"}), "url"));
    sections.push_back(contact);

    Section security{QObject::tr("Security headers"), {}};
    list(security.changes, {"security", "missing"}, QObject::tr("Missing header"));
    sections.push_back(security);

    Section sitemap{QObject::tr("Sitemap"), {}};
    sc(sitemap.changes, {"sitemaps", "totalUrls"}, QObject::tr("Sitemap URLs"));
    sections.push_back(sitemap);

    QVariantList groups;
    QStringList unchanged;
    int count = 0;
    for (const Section &section : sections) {
        if (section.changes.isEmpty()) {
            unchanged.append(section.name);
            continue;
        }
        count += static_cast<int>(section.changes.size());
        groups.append(QVariantMap{{QStringLiteral("section"), section.name},
                                  {QStringLiteral("changes"), section.changes}});
    }
    return {{QStringLiteral("groups"), groups},
            {QStringLiteral("unchangedSections"), unchanged},
            {QStringLiteral("count"), count}};
}

} // namespace SnapshotDiff
