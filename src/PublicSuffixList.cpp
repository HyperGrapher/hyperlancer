#include "PublicSuffixList.h"

#include <algorithm>

#include <QFile>
#include <QStringList>
#include <QTextStream>
#include <QUrl>

PublicSuffixList::PublicSuffixList(const QString &file)
{
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("Public Suffix List not readable: %s", qPrintable(file));
        return;
    }
    QTextStream stream(&input);
    bool inIcann = false;
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.contains(QStringLiteral("===BEGIN ICANN DOMAINS==="))) {
            inIcann = true;
            continue;
        }
        if (line.contains(QStringLiteral("===END ICANN DOMAINS==="))) {
            inIcann = false;
            continue;
        }
        if (!inIcann || line.isEmpty() || line.startsWith(QStringLiteral("//"))) {
            continue;
        }
        const QString rule = line.section(QChar(u' '), 0, 0).toLower();
        m_rules.insert(rule);
        const QStringList labels = rule.split(QChar(u'.'));
        bool ascii = true;
        for (const QString &label : labels) {
            for (const QChar c : label) {
                if (c.unicode() > 127) {
                    ascii = false;
                }
            }
        }
        // Hosts are normalized to punycode, so non-ASCII rules are stored in that form too.
        if (!ascii) {
            QStringList encoded;
            for (const QString &label : labels) {
                encoded.append(label == QStringLiteral("*") || label.startsWith(u'!')
                                   ? label
                                   : QString::fromLatin1(QUrl::toAce(label)));
            }
            m_rules.insert(encoded.join(QChar(u'.')));
        }
    }
}

const PublicSuffixList &PublicSuffixList::bundled()
{
    static const PublicSuffixList list(QStringLiteral(":/data/public_suffix_list.dat"));
    return list;
}

int PublicSuffixList::suffixLength(const QString &host) const
{
    const QStringList labels = host.split(QChar(u'.'));
    const auto count = static_cast<int>(labels.size());
    int best = 1; // the implicit "*" rule
    for (int k = 1; k <= count; ++k) {
        const QString suffix = labels.mid(count - k).join(QChar(u'.'));
        if (m_rules.contains(QStringLiteral("!") + suffix)) {
            return k - 1; // an exception rule wins outright
        }
        const bool wildcard =
            k > 1 &&
            m_rules.contains(QStringLiteral("*.") + labels.mid(count - k + 1).join(QChar(u'.')));
        if (m_rules.contains(suffix) || wildcard) {
            best = std::max(best, k);
        }
    }
    return best;
}

QString PublicSuffixList::registrableDomain(const QString &host) const
{
    const QStringList labels = host.split(QChar(u'.'));
    const int length = suffixLength(host);
    if (labels.size() <= length) {
        return {};
    }
    return labels.mid(labels.size() - length - 1).join(QChar(u'.'));
}
