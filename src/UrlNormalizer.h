#pragma once

#include <QString>

class PublicSuffixList;

// Turns what the user typed or a search result URL into the canonical form used as the
// site's identity. Same rules as the profiler's normalize_url and the old PHP UrlNormalizer:
// https is assumed, the host is lower-case punycode, "www." is dropped from the site key.
struct NormalizedUrl
{
    QString url;               // e.g. "https://www.example.com.tr/"
    QString host;              // "www.example.com.tr"
    QString siteKey;           // "example.com.tr" (port appended when not default)
    QString registrableDomain; // "example.com.tr"; empty for IP addresses
    bool isIp = false;
};

struct UrlNormalizeResult
{
    NormalizedUrl value;
    QString error; // empty on success

    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

[[nodiscard]] UrlNormalizeResult normalizeUrl(const QString &input,
                                              const PublicSuffixList &suffixes);

// Only the scheme, host and port, with the path reduced to "/". Used when a search result
// points deep into a site but the site itself is what we track.
[[nodiscard]] UrlNormalizeResult normalizeSiteRoot(const QString &input,
                                                   const PublicSuffixList &suffixes);
