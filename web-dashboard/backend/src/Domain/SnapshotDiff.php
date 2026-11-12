<?php
declare(strict_types=1);

namespace App\Domain;

/**
 * Semantic diff over two normalised summaries (docs/DECISIONS.md D13). Volatile noise (timings, cf-ray, cache headers)
 * is not part of the summary or is ignored here, so every reported change is something worth knowing.
 */
final class SnapshotDiff
{
    /** @var list<string> */
    public const SECTIONS = ['Technologies', 'Hosting', 'DNS and email', 'SSL / TLS', 'Domain', 'SEO', 'HTTP', 'Contact', 'Security headers', 'Sitemap'];

    /**
     * @param array<string,mixed> $before older summary
     * @param array<string,mixed> $after  newer summary
     * @return array{groups:list<array{section:string,changes:list<array<string,mixed>>}>,unchangedSections:list<string>,count:int}
     */
    public function diff(array $before, array $after): array
    {
        $groups = [];
        $add = static function (string $section, array $changes) use (&$groups): void {
            if ($changes) {
                $groups[] = ['section' => $section, 'changes' => $changes];
            }
        };

        $add('Technologies', $this->technologies($before['tech'] ?? [], $after['tech'] ?? []));

        $add('Hosting', array_merge(
            $this->set('hosting.ips', 'IP address', $before['hosting']['ips'] ?? [], $after['hosting']['ips'] ?? []),
            $this->set('hosting.ipv6', 'IPv6 address', $before['hosting']['ipv6'] ?? [], $after['hosting']['ipv6'] ?? []),
            $this->scalar('hosting.org', 'Hosting network', $before['hosting']['org'] ?? null, $after['hosting']['org'] ?? null),
            $this->scalar('hosting.edge', 'Edge / CDN', $before['hosting']['edge'] ?? null, $after['hosting']['edge'] ?? null),
            $this->scalar('hosting.server', 'Web server', $before['hosting']['server'] ?? null, $after['hosting']['server'] ?? null),
            $this->scalar('hosting.language', 'Backend language', $before['hosting']['language'] ?? null, $after['hosting']['language'] ?? null),
            $this->scalar('hosting.languageVersion', 'Language version', $before['hosting']['languageVersion'] ?? null, $after['hosting']['languageVersion'] ?? null),
            $this->scalar('cms.name', 'CMS', $before['cms']['name'] ?? null, $after['cms']['name'] ?? null),
            $this->scalar('cms.version', 'CMS version', $before['cms']['version'] ?? null, $after['cms']['version'] ?? null),
            $this->scalar('cms.theme', 'WordPress theme', $before['cms']['theme'] ?? null, $after['cms']['theme'] ?? null),
            $this->set('cms.plugins', 'WordPress plugin', $before['cms']['plugins'] ?? [], $after['cms']['plugins'] ?? []),
        ));

        $add('DNS and email', array_merge(
            $this->set('dns.ns', 'Nameserver', $before['dns']['ns'] ?? [], $after['dns']['ns'] ?? []),
            $this->set('dns.mx', 'Mail server', array_column($before['dns']['mx'] ?? [], 'host'), array_column($after['dns']['mx'] ?? [], 'host')),
            $this->scalar('dns.emailProvider', 'Email provider', $before['dns']['emailProvider'] ?? null, $after['dns']['emailProvider'] ?? null),
            $this->scalar('dns.spf', 'SPF', $before['dns']['spf'] ?? null, $after['dns']['spf'] ?? null),
            $this->scalar('dns.dmarc', 'DMARC', $before['dns']['dmarc'] ?? null, $after['dns']['dmarc'] ?? null),
            $this->scalar('dns.dnssec', 'DNSSEC', $before['dns']['dnssec'] ?? null, $after['dns']['dnssec'] ?? null),
            $this->set('dns.caa', 'CAA record', $before['dns']['caa'] ?? [], $after['dns']['caa'] ?? []),
        ));

        $add('SSL / TLS', array_merge(
            $this->scalar('tls.issuer', 'Issuer', $before['tls']['issuer'] ?? null, $after['tls']['issuer'] ?? null),
            $this->scalar('tls.validUntil', 'Valid until', $this->day($before['tls']['validUntil'] ?? null), $this->day($after['tls']['validUntil'] ?? null)),
            $this->scalar('tls.verified', 'Certificate trusted', $before['tls']['verified'] ?? null, $after['tls']['verified'] ?? null),
        ));

        $add('Domain', array_merge(
            $this->scalar('domain.registrar', 'Registrar', $before['domain']['registrar'] ?? null, $after['domain']['registrar'] ?? null),
            $this->scalar('domain.registrant', 'Registrant', $before['domain']['registrant'] ?? null, $after['domain']['registrant'] ?? null),
            $this->scalar('domain.expires', 'Expires', $this->day($before['domain']['expires'] ?? null), $this->day($after['domain']['expires'] ?? null)),
            $this->scalar('domain.transferLocked', 'Transfer lock', $before['domain']['transferLocked'] ?? null, $after['domain']['transferLocked'] ?? null),
            $this->set('domain.statuses', 'Domain status', $before['domain']['statuses'] ?? [], $after['domain']['statuses'] ?? []),
        ));

        $add('SEO', array_merge(
            $this->scalar('seo.title', 'Title', $before['seo']['title'] ?? null, $after['seo']['title'] ?? null),
            $this->scalar('seo.description', 'Meta description', $before['seo']['description'] ?? null, $after['seo']['description'] ?? null),
            $this->scalar('seo.canonical', 'Canonical URL', $before['seo']['canonical'] ?? null, $after['seo']['canonical'] ?? null),
            $this->scalar('seo.robots', 'Meta robots', $before['seo']['robots'] ?? null, $after['seo']['robots'] ?? null),
            $this->scalar('seo.lang', 'Language', $before['seo']['lang'] ?? null, $after['seo']['lang'] ?? null),
            $this->scalar('seo.h1Count', 'H1 count', $before['seo']['h1Count'] ?? null, $after['seo']['h1Count'] ?? null),
            $this->set('seo.structuredData', 'Structured data type', $before['seo']['structuredData'] ?? [], $after['seo']['structuredData'] ?? []),
        ));

        $add('HTTP', array_merge(
            $this->scalar('http.status', 'HTTP status', $before['http']['status'] ?? null, $after['http']['status'] ?? null),
            $this->scalar('url.final', 'Final URL', $before['url']['final'] ?? null, $after['url']['final'] ?? null),
            $this->scalar('http.compression', 'Compression', $before['http']['compression'] ?? null, $after['http']['compression'] ?? null),
            $this->scalar('http.httpsRedirect', 'HTTP to HTTPS redirect', $before['http']['httpsRedirect'] ?? null, $after['http']['httpsRedirect'] ?? null),
            $this->scalar('http.wwwCanonical', 'Canonical host', $before['http']['wwwCanonical'] ?? null, $after['http']['wwwCanonical'] ?? null),
        ));

        $add('Contact', array_merge(
            $this->set('contact.emails', 'Email', $before['contact']['emails'] ?? [], $after['contact']['emails'] ?? []),
            $this->set('contact.phones', 'Phone', $before['contact']['phones'] ?? [], $after['contact']['phones'] ?? []),
            $this->set('contact.socials', 'Social profile', array_column($before['contact']['socials'] ?? [], 'url'), array_column($after['contact']['socials'] ?? [], 'url')),
        ));

        $add('Security headers', $this->set('security.missing', 'Missing header', $before['security']['missing'] ?? [], $after['security']['missing'] ?? []));

        $add('Sitemap', $this->scalar('sitemaps.totalUrls', 'Sitemap URLs', $before['sitemaps']['totalUrls'] ?? null, $after['sitemaps']['totalUrls'] ?? null));

        $count = 0;
        foreach ($groups as $group) {
            $count += count($group['changes']);
        }
        $changed = array_column($groups, 'section');
        return ['groups' => $groups, 'unchangedSections' => array_values(array_diff(self::SECTIONS, $changed)), 'count' => $count];
    }

    private function day(mixed $value): ?string
    {
        return is_string($value) && $value !== '' ? substr($value, 0, 10) : null;
    }

    private function show(mixed $value): ?string
    {
        if ($value === null || $value === '') {
            return null;
        }
        return is_bool($value) ? ($value ? 'yes' : 'no') : (string) $value;
    }

    /** @return list<array<string,mixed>> */
    private function scalar(string $path, string $label, mixed $before, mixed $after): array
    {
        $a = $this->show($before);
        $b = $this->show($after);
        if ($a === $b) {
            return [];
        }
        $kind = $a === null ? 'added' : ($b === null ? 'removed' : 'changed');
        return [['path' => $path, 'label' => $label, 'kind' => $kind, 'before' => $a, 'after' => $b]];
    }

    /** @param list<mixed> $before @param list<mixed> $after @return list<array<string,mixed>> */
    private function set(string $path, string $label, array $before, array $after): array
    {
        $a = array_values(array_unique(array_map('strval', $before)));
        $b = array_values(array_unique(array_map('strval', $after)));
        $changes = [];
        foreach (array_diff($a, $b) as $item) {
            $changes[] = ['path' => $path, 'label' => $label, 'kind' => 'removed', 'before' => $item, 'after' => null];
        }
        foreach (array_diff($b, $a) as $item) {
            $changes[] = ['path' => $path, 'label' => $label, 'kind' => 'added', 'before' => null, 'after' => $item];
        }
        return $changes;
    }

    /** Technologies are matched on category and name; a version change is "changed". @return list<array<string,mixed>> */
    private function technologies(array $before, array $after): array
    {
        $index = static function (array $list): array {
            $map = [];
            foreach ($list as $t) {
                $map[$t['category'] . '|' . $t['name']] = $t;
            }
            return $map;
        };
        $a = $index($before);
        $b = $index($after);
        $changes = [];
        foreach ($a as $key => $t) {
            if (!isset($b[$key])) {
                $changes[] = ['path' => "tech.$key", 'label' => $t['category'], 'kind' => 'removed', 'before' => trim($t['name'] . ' ' . ($t['version'] ?? '')), 'after' => null];
            }
        }
        foreach ($b as $key => $t) {
            if (!isset($a[$key])) {
                $changes[] = ['path' => "tech.$key", 'label' => $t['category'], 'kind' => 'added', 'before' => null, 'after' => trim($t['name'] . ' ' . ($t['version'] ?? ''))];
            } elseif (($a[$key]['version'] ?? null) !== ($t['version'] ?? null)) {
                $changes[] = ['path' => "tech.$key", 'label' => $t['category'] . ' · ' . $t['name'], 'kind' => 'changed',
                    'before' => $a[$key]['version'] ?? null, 'after' => $t['version'] ?? null];
            }
        }
        return $changes;
    }
}
