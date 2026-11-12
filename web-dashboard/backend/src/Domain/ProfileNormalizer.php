<?php
declare(strict_types=1);

namespace App\Domain;

/**
 * Raw profiler output (schema v1 from the old script, v2 from `python -m site_profile`) to the version-independent
 * summary the UI and the diff read (docs/PRD.md §7.3). Every key is always present; unknown values are null or empty.
 */
final class ProfileNormalizer
{
    public const SECURITY_HEADERS = [
        'Strict-Transport-Security', 'Content-Security-Policy', 'X-Frame-Options',
        'X-Content-Type-Options', 'Referrer-Policy', 'Permissions-Policy',
    ];

    /** @param array<string,mixed> $raw @return array<string,mixed> */
    public function normalize(array $raw): array
    {
        $v2 = (int) ($raw['schema_version'] ?? 1) >= 2;
        $errors = $v2 ? $this->v2Errors($raw) : $this->legacyErrors($raw);
        $outcome = $v2 ? $this->outcomeV2((string) ($raw['status'] ?? '')) : $this->legacyOutcome($raw, $errors);

        $tech = $this->technologies($raw);
        $hostRecords = $this->get($raw, 'dns.website_host_records.records', []);
        $domainRecords = $this->get($raw, 'dns.domain_records.records', []);
        $wordpress = (array) ($raw['wordpress'] ?? []);
        $page = (array) ($raw['page'] ?? []);
        $parseable = (bool) ($page['parseable_html'] ?? false);

        $cms = $this->cms($tech, $wordpress, $parseable);
        $language = $this->firstTech($tech, 'Likely backend language');
        $serverHint = $this->firstTech($tech, 'Server-side hint');
        $languageVersion = null;
        if ($language && $serverHint && stripos((string) $serverHint['name'], (string) $language['name']) === 0
            && preg_match('#/([\d.]+)#', (string) $serverHint['name'], $m)) {
            $languageVersion = $m[1];
        }
        $httpServer = $this->firstTech($tech, 'HTTP server header');
        $edge = $this->firstTech($tech, 'Hosting / edge');

        $ips = array_values(array_map('strval', (array) ($raw['hosting']['ips'] ?? ($hostRecords['A'] ?? []))));
        $ipv6 = array_values(array_map('strval', (array) ($raw['hosting']['ipv6'] ?? ($hostRecords['AAAA'] ?? []))));
        $asName = $this->get($raw, 'hosting.as_name');

        $reg = (array) ($raw['domain_registration'] ?? []);
        $nsValues = $v2 ? (array) $this->get($raw, 'dns.ns.values', []) : (array) ($domainRecords['NS'] ?? []);
        $nsSource = $v2 ? $this->get($raw, 'dns.ns.source') : ($nsValues ? 'dns' : null);
        if (!$nsValues && !empty($reg['nameservers'])) {
            $nsValues = (array) $reg['nameservers'];
            $nsSource = 'registry';
        }
        $mx = $this->mx((array) ($domainRecords['MX'] ?? []));

        $tls = (array) ($raw['tls_certificate'] ?? []);
        $security = (array) $this->get($raw, 'http.security_headers.present', []);
        $present = array_keys(array_filter($security, static fn ($v) => $v !== null && $v !== ''));
        $missing = $this->get($raw, 'http.security_headers.missing');
        $missing = is_array($missing) ? array_values($missing) : array_values(array_diff(self::SECURITY_HEADERS, $present));

        $og = (array) ($page['open_graph'] ?? []);
        $headings = array_values(array_map(
            static fn ($h) => ['level' => (int) ($h['level'] ?? 0), 'text' => (string) ($h['text'] ?? '')],
            (array) ($page['headings'] ?? [])
        ));

        return [
            'schemaVersion' => $v2 ? 2 : 1,
            'outcome' => $outcome,
            'errors' => $errors,
            'url' => [
                'requested' => $this->get($raw, 'input.requested_url'),
                'final' => $this->get($raw, 'http.final_url'),
                'redirects' => array_values(array_map(
                    static fn ($r) => ['status' => (int) ($r['status'] ?? 0), 'from' => (string) ($r['from'] ?? ''), 'to' => (string) ($r['to'] ?? '')],
                    (array) $this->get($raw, 'http.redirects', [])
                )),
            ],
            'http' => [
                'status' => $this->int($this->get($raw, 'http.status')),
                'ttfbMs' => $this->int($this->get($raw, 'http.ttfb_ms')),
                'totalMs' => $this->int($this->get($raw, 'http.total_ms')),
                'htmlBytes' => $v2 ? $this->int($this->get($raw, 'http.html_bytes')) : null,
                'compression' => $this->get($raw, 'http.compression'),
                'httpVersion' => $this->get($raw, 'http.http_version'),
                'httpsRedirect' => $this->get($raw, 'http.checks.https_redirect'),
                'wwwCanonical' => $this->get($raw, 'http.checks.www_canonical'),
                'wwwRedirect' => $this->get($raw, 'http.checks.www_redirect'),
                'blocked' => (bool) $this->get($raw, 'http.bot_protection.detected', false),
                'blockedBy' => $this->get($raw, 'http.bot_protection.provider'),
                'error' => $this->get($raw, 'http.error'),
            ],
            'tech' => $tech,
            'cms' => [
                'name' => $cms['name'],
                'version' => $cms['version'],
                'theme' => $wordpress['theme'] ?? null,
                'plugins' => array_values((array) ($wordpress['plugins'] ?? [])),
            ],
            'hosting' => [
                'ips' => $ips,
                'ipv6' => $ipv6,
                'org' => $asName,
                'asn' => $this->get($raw, 'hosting.asn'),
                'prefix' => $this->get($raw, 'hosting.prefix'),
                'country' => $this->get($raw, 'hosting.country'),
                'reverseDns' => $this->get($raw, 'hosting.reverse_dns'),
                'edge' => $edge['name'] ?? null,
                'server' => $httpServer['name'] ?? ($this->get($raw, 'http.notable_headers.server')),
                'language' => $language['name'] ?? null,
                'languageVersion' => $languageVersion,
            ],
            'dns' => [
                'ns' => array_values(array_map('strval', $nsValues)),
                'nsSource' => $nsSource,
                'mx' => $mx,
                'emailProvider' => $v2 ? $this->get($raw, 'email.provider') : null,
                'spf' => ($this->get($raw, 'dns.domain_records.spf_records', [])[0] ?? null),
                'dmarc' => ($this->get($raw, 'dns.domain_records.dmarc_records', [])[0] ?? null),
                'dnssec' => (bool) $this->get($raw, 'dns.domain_records.dnskey_present', false),
                'caa' => array_values((array) ($domainRecords['CAA'] ?? [])),
                'txt' => array_values((array) ($domainRecords['TXT'] ?? [])),
            ],
            'tls' => [
                'verified' => (bool) ($tls['connection_verified'] ?? false),
                'issuer' => $this->issuer($tls['issuer'] ?? null),
                'validFrom' => $tls['valid_from_utc'] ?? null,
                'validUntil' => $tls['valid_until_utc'] ?? null,
                'daysLeft' => isset($tls['days_until_expiry']) ? (float) $tls['days_until_expiry'] : null,
                'sanCount' => (int) ($tls['dns_subject_alt_name_count'] ?? 0),
                'sans' => array_values((array) ($tls['dns_subject_alt_names_sample'] ?? [])),
                'fingerprint' => $tls['sha256_fingerprint'] ?? null,
                'error' => $tls['error'] ?? null,
            ],
            'domain' => [
                'registrar' => $reg['registrar'] ?? null,
                'registrant' => $reg['registrant'] ?? null,
                'created' => $reg['registration_date'] ?? null,
                'expires' => $reg['expiration_date'] ?? null,
                'updated' => $reg['last_updated'] ?? null,
                'ageYears' => isset($reg['domain_age_years']) ? (float) $reg['domain_age_years'] : null,
                'statuses' => array_values((array) ($reg['statuses'] ?? [])),
                'transferLocked' => $reg['transfer_locked'] ?? null,
                'method' => $reg['lookup_method'] ?? null,
                'source' => $reg['source'] ?? null,
            ],
            'seo' => [
                'title' => $page['title'] ?? null,
                'titleLength' => (int) ($page['title_length'] ?? 0),
                'description' => $page['meta_description'] ?? null,
                'descriptionLength' => (int) ($page['meta_description_length'] ?? 0),
                'canonical' => $page['canonical_url'] ?? null,
                'robots' => $page['meta_robots'] ?? null,
                'noindex' => (bool) ($page['noindex'] ?? stripos((string) ($page['meta_robots'] ?? ''), 'noindex') !== false),
                'lang' => $page['html_language'] ?? null,
                'hreflang' => array_values((array) ($page['hreflang'] ?? [])),
                'ogTitle' => $og['title'] ?? ($page['open_graph_title'] ?? null),
                'ogImage' => $og['image'] ?? ($page['open_graph_image'] ?? null),
                'og' => $og,
                'twitter' => (array) ($page['twitter'] ?? []),
                'structuredData' => array_values((array) ($page['structured_data_types'] ?? [])),
                'h1Count' => (int) ($page['h1_count'] ?? 0),
                'headings' => $headings,
                'imagesTotal' => (int) ($page['image_count'] ?? 0),
                'imagesMissingAlt' => (int) ($page['images_missing_alt'] ?? 0),
                'links' => $page['links'] ?? null,
                'consentTool' => $page['consent_tool'] ?? null,
                'favicon' => $page['favicon_url'] ?? null,
                'parseable' => $parseable,
            ],
            'sitemaps' => $this->sitemaps($raw, $v2),
            'contact' => [
                'emails' => array_values((array) $this->get($raw, 'contacts.emails', [])),
                'phones' => array_values((array) $this->get($raw, 'contacts.phones', [])),
                'address' => $this->get($raw, 'contacts.address'),
                'socials' => array_values((array) $this->get($raw, 'contacts.socials', [])),
                'whatsapp' => array_values((array) $this->get($raw, 'contacts.whatsapp', [])),
            ],
            'security' => [
                'present' => $present,
                'missing' => $missing,
                'cookies' => array_values(array_map(static fn ($c) => [
                    'name' => (string) ($c['name'] ?? ''),
                    'secure' => (bool) ($c['secure'] ?? false),
                    'httpOnly' => (bool) ($c['http_only'] ?? false),
                    'sameSite' => $c['same_site'] ?? null,
                ], (array) $this->get($raw, 'http.cookies', []))),
            ],
            'archive' => [
                'first' => $this->date($this->get($raw, 'website_history.earliest_capture_utc')),
                'firstUrl' => $this->get($raw, 'website_history.earliest_capture_archive_url'),
                'last' => $this->date($this->get($raw, 'website_history.latest_capture_utc')),
                'lastUrl' => $this->get($raw, 'website_history.latest_capture_archive_url'),
            ],
            'design' => [
                'estimatedFrom' => $this->get($raw, 'design.estimated_from'),
                'latestAsset' => $this->get($raw, 'design.latest_asset'),
                'confidence' => $this->get($raw, 'design.confidence'),
                'basis' => array_values((array) $this->get($raw, 'design.basis', [])),
                'clues' => array_values(array_map(static fn ($c) => [
                    'kind' => (string) ($c['kind'] ?? ''), 'label' => (string) ($c['label'] ?? ''), 'date' => (string) ($c['date'] ?? ''),
                    'detail' => $c['detail'] ?? null, 'strength' => (string) ($c['strength'] ?? 'weak'),
                ], (array) $this->get($raw, 'design.clues', []))),
            ],
            'limitations' => array_values((array) ($raw['limitations'] ?? [])),
        ];
    }

    /**
     * The columns copied onto `sites` (docs/DECISIONS.md D3).
     *
     * @param array<string,mixed> $summary @return array<string,mixed>
     */
    public function facts(array $summary): array
    {
        $org = (string) ($summary['hosting']['org'] ?? '');
        if ($org !== '') {
            $org = preg_replace('/,\s*[A-Z]{2}$/', '', $org) ?? $org;
            $org = trim(str_contains($org, ' - ') ? substr($org, (int) strpos($org, ' - ') + 3) : $org);
        }
        $tlsUntil = $summary['tls']['validUntil'] ?? null;
        return [
            'http_status' => $summary['http']['status'],
            'final_url' => $summary['url']['final'],
            'title' => $summary['seo']['title'],
            'cms' => $summary['cms']['name'],
            'cms_version' => $summary['cms']['version'],
            'server' => $summary['hosting']['server'],
            'hosting' => $summary['hosting']['edge'] ?: ($org !== '' ? $org : null),
            'backend_language' => $summary['hosting']['language'],
            'ip_address' => $summary['hosting']['ips'][0] ?? ($summary['hosting']['ipv6'][0] ?? null),
            'tls_valid_until' => $tlsUntil ? substr((string) $tlsUntil, 0, 10) : null,
            'domain_expires_on' => $summary['domain']['expires'] ? substr((string) $summary['domain']['expires'], 0, 10) : null,
            'registrar' => $summary['domain']['registrar'],
            'favicon_url' => $summary['seo']['favicon'],
        ];
    }

    /** Summary for an attempt that produced no report at all (crash, missing file). */
    public function failed(string $message, string $section = 'job'): array
    {
        return $this->normalize(['schema_version' => 2, 'status' => 'failed', 'errors' => [['section' => $section, 'code' => 'crash', 'message' => $message]]]);
    }

    // ---- helpers ---------------------------------------------------------------------------------------------

    /** @param array<string,mixed> $data @param mixed $default @return mixed */
    private function get(array $data, string $path, mixed $default = null): mixed
    {
        foreach (explode('.', $path) as $key) {
            if (!is_array($data) || !array_key_exists($key, $data)) {
                return $default;
            }
            $data = $data[$key];
        }
        return $data ?? $default;
    }

    private function int(mixed $value): ?int
    {
        return is_numeric($value) ? (int) $value : null;
    }

    private function date(mixed $value): ?string
    {
        return is_string($value) && $value !== '' ? substr($value, 0, 10) : null;
    }

    private function outcomeV2(string $status): string
    {
        return in_array($status, ['ok', 'partial', 'failed'], true) ? $status : 'failed';
    }

    /** @return list<array{section:string,code:string,message:string}> */
    private function v2Errors(array $raw): array
    {
        return array_values(array_map(static fn ($e) => [
            'section' => (string) ($e['section'] ?? ''), 'code' => (string) ($e['code'] ?? ''), 'message' => (string) ($e['message'] ?? ''),
        ], (array) ($raw['errors'] ?? [])));
    }

    /** Schema v1 has no error list: derive one from the free-text error fields. @return list<array{section:string,code:string,message:string}> */
    private function legacyErrors(array $raw): array
    {
        $errors = [];
        $add = static function (string $section, mixed $message) use (&$errors): void {
            if (is_string($message) && $message !== '') {
                $errors[] = ['section' => $section, 'code' => 'legacy', 'message' => $message];
            }
        };
        $add('http', $this->get($raw, 'http.error'));
        $add('page', $this->get($raw, 'page.error'));
        $add('tls', $this->get($raw, 'tls_certificate.error'));
        $add('registration', $this->get($raw, 'domain_registration.error'));
        $add('archive', $this->get($raw, 'website_history.error'));
        $add('sitemaps', $this->get($raw, 'robots_and_sitemaps.robots.error'));
        foreach ((array) $this->get($raw, 'dns.domain_records.errors', []) as $type => $message) {
            $add('dns', "$type: $message");
        }
        return $errors;
    }

    /** @param list<array<string,string>> $errors */
    private function legacyOutcome(array $raw, array $errors): string
    {
        $status = $this->get($raw, 'http.status');
        if ($this->get($raw, 'http.error') || !is_numeric($status)) {
            return 'failed';
        }
        return $errors ? 'partial' : 'ok';
    }

    /** @return list<array{category:string,name:string,version:?string,confidence:string,evidence:list<string>}> */
    private function technologies(array $raw): array
    {
        $out = [];
        foreach ((array) ($raw['technologies'] ?? []) as $t) {
            if (!isset($t['name'])) {
                continue;
            }
            $out[] = [
                'category' => (string) ($t['category'] ?? 'Other'),
                'name' => (string) $t['name'],
                'version' => isset($t['version']) ? (string) $t['version'] : null,
                'confidence' => in_array($t['confidence'] ?? '', ['low', 'medium', 'high'], true) ? $t['confidence'] : 'low',
                'evidence' => array_values(array_map('strval', (array) ($t['evidence'] ?? []))),
            ];
        }
        return $out;
    }

    /** @param list<array<string,mixed>> $tech @return ?array<string,mixed> */
    private function firstTech(array $tech, string $category): ?array
    {
        foreach ($tech as $t) {
            if ($t['category'] === $category) {
                return $t;
            }
        }
        return null;
    }

    /** @return array{name:?string,version:?string} */
    private function cms(array $tech, array $wordpress, bool $parseable): array
    {
        $cms = $this->firstTech($tech, 'CMS');
        if ($cms) {
            return ['name' => $cms['name'], 'version' => $cms['version'] ?: ($cms['name'] === 'WordPress' ? ($wordpress['version'] ?? null) : null)];
        }
        // No known CMS on a page that loaded: a hand-built or unrecognised site.
        return ['name' => $parseable ? 'Custom' : null, 'version' => null];
    }

    /** @param list<string> $records @return list<array{pref:int,host:string}> */
    private function mx(array $records): array
    {
        $out = [];
        foreach ($records as $record) {
            if (preg_match('/^(\d+)\s+(\S+)$/', (string) $record, $m)) {
                $out[] = ['pref' => (int) $m[1], 'host' => strtolower($m[2])];
            }
        }
        usort($out, static fn ($a, $b) => [$a['pref'], $a['host']] <=> [$b['pref'], $b['host']]);
        return $out;
    }

    /** "countryName=GB, organizationName=Sectigo Limited, commonName=R36" -> "Sectigo Limited". */
    private function issuer(mixed $issuer): ?string
    {
        if (!is_string($issuer) || $issuer === '') {
            return null;
        }
        foreach (['organizationName', 'commonName'] as $field) {
            if (preg_match('/' . $field . '=([^,]+)/', $issuer, $m)) {
                return trim($m[1]);
            }
        }
        return $issuer;
    }

    /** @return array<string,mixed> */
    private function sitemaps(array $raw, bool $v2): array
    {
        $robots = (array) $this->get($raw, 'robots_and_sitemaps.robots', []);
        $base = [
            'robotsStatus' => $this->int($robots['status'] ?? null),
            'disallowRules' => (int) ($robots['disallow_rule_count'] ?? 0),
            'allowRules' => (int) ($robots['allow_rule_count'] ?? 0),
            'crawlDelay' => $robots['crawl_delay'] ?? null,
        ];
        if ($v2) {
            $s = (array) $this->get($raw, 'robots_and_sitemaps.sitemaps', []);
            return $base + [
                'files' => array_values(array_map(static fn ($f) => [
                    'url' => (string) ($f['url'] ?? ''), 'kind' => (string) ($f['kind'] ?? ''), 'urls' => $f['urls'] ?? null,
                    'error' => $f['error'] ?? null, 'warning' => $f['warning'] ?? null,
                ], (array) ($s['files'] ?? []))),
                'totalUrls' => (int) ($s['total_urls'] ?? 0),
                'urls' => array_values(array_map(static fn ($u) => ['loc' => (string) ($u['loc'] ?? ''), 'lastmod' => $u['lastmod'] ?? null], (array) ($s['urls'] ?? []))),
                'truncated' => (bool) ($s['truncated'] ?? false),
            ];
        }
        $files = [];
        $urls = [];
        $total = 0;
        foreach ((array) $this->get($raw, 'robots_and_sitemaps.sitemaps', []) as $f) {
            $count = $f['url_count'] ?? null;
            $files[] = ['url' => (string) ($f['url'] ?? ''), 'kind' => (string) ($f['kind'] ?? ''), 'urls' => $count,
                'error' => $f['error'] ?? ($f['parse_error'] ?? null), 'warning' => null];
            $total += (int) $count;
            foreach ((array) ($f['sample_locations'] ?? []) as $loc) {
                $urls[] = ['loc' => (string) $loc, 'lastmod' => null];
            }
        }
        return $base + ['files' => $files, 'totalUrls' => $total, 'urls' => $urls, 'truncated' => true];
    }
}
