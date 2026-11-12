#pragma once

#include <QSet>
#include <QString>

// Registrable domain (eTLD+1) from the bundled Public Suffix List, ICANN section only, the
// same rules the profiler (tldextract) uses.
class PublicSuffixList
{
public:
    explicit PublicSuffixList(const QString &file);

    // The list shipped in the resources, loaded once.
    [[nodiscard]] static const PublicSuffixList &bundled();

    [[nodiscard]] bool isEmpty() const { return m_rules.isEmpty(); }
    // Number of labels in the public suffix of a lower-case ASCII host.
    [[nodiscard]] int suffixLength(const QString &host) const;
    // Empty when the host is itself a public suffix.
    [[nodiscard]] QString registrableDomain(const QString &host) const;

private:
    QSet<QString> m_rules;
};
