<?php
declare(strict_types=1);

namespace App\Domain;

use App\Http\HttpException;
use App\Jobs\JobRepository;
use App\Security\NetworkGuard;
use InvalidArgumentException;
use PDO;

/** Site CRUD, tags and the shapes the API returns (docs/PRD.md §8.2). */
final class SiteService
{
    public function __construct(private readonly PDO $pdo)
    {
    }

    // ---- reading ---------------------------------------------------------------------------------------------

    /** @param list<array<string,mixed>> $rows rows from `sites s LEFT JOIN categories c` @return list<array<string,mixed>> */
    public function listItems(array $rows): array
    {
        if (!$rows) {
            return [];
        }
        $ids = array_map(static fn ($r) => (int) $r['id'], $rows);
        $stmt = $this->pdo->prepare(
            'SELECT st.site_id, t.id, t.name, t.color FROM site_tags st JOIN tags t ON t.id = st.tag_id
             WHERE st.site_id IN (' . implode(',', array_fill(0, count($ids), '?')) . ') ORDER BY t.name COLLATE NOCASE'
        );
        $stmt->execute($ids);
        $tags = [];
        foreach ($stmt->fetchAll() as $t) {
            $tags[(int) $t['site_id']][] = ['id' => (int) $t['id'], 'name' => $t['name'], 'color' => $t['color']];
        }
        return array_map(fn ($r) => $this->listItem($r, $tags[(int) $r['id']] ?? []), $rows);
    }

    /** @param array<string,mixed> $r @param list<array<string,mixed>> $tags @return array<string,mixed> */
    private function listItem(array $r, array $tags): array
    {
        return [
            'id' => (int) $r['id'],
            'siteKey' => $r['site_key'],
            'url' => $r['url'],
            'name' => $r['name'],
            'faviconUrl' => $r['favicon_url'],
            'previewUrl' => $r['preview_url'] ?? null,
            'status' => $r['status'],
            'category' => $r['category_id'] === null ? null : ['id' => (int) $r['category_id'], 'name' => $r['category_name'] ?? null, 'color' => $r['category_color'] ?? null],
            'tags' => $tags,
            'cms' => $r['cms'],
            'cmsVersion' => $r['cms_version'],
            'server' => $r['server'],
            'hosting' => $r['hosting'],
            'lastFetchedAt' => $r['last_fetched_at'],
            'lastFetchOutcome' => $r['last_fetch_outcome'],
            'tlsValidUntil' => $r['tls_valid_until'],
            'domainExpiresOn' => $r['domain_expires_on'],
            'openTodos' => (int) ($r['open_todos'] ?? 0),
            'deadline' => $r['deadline'],
            'archivedAt' => $r['archived_at'],
            'updatedAt' => $r['updated_at'],
        ];
    }

    /** @return array<string,mixed>|null raw row with category columns and the open-to-do count */
    private function row(int $id): ?array
    {
        $stmt = $this->pdo->prepare(
            'SELECT s.*, c.name AS category_name, c.color AS category_color,
                    (SELECT count(*) FROM todos td WHERE td.site_id = s.id AND td.done = 0) AS open_todos
             FROM sites s LEFT JOIN categories c ON c.id = s.category_id WHERE s.id = ?'
        );
        $stmt->execute([$id]);
        return $stmt->fetch() ?: null;
    }

    public function requireRow(int $id): array
    {
        return $this->row($id) ?? throw new HttpException(404, 'not_found', 'Site not found.');
    }

    /** @return array<string,mixed> */
    public function detail(int $id): array
    {
        $row = $this->requireRow($id);
        $item = $this->listItems([$row])[0];

        $snap = fn (?int $sid) => $sid ? $this->snapshotMeta($sid) : null;
        $lastAttempt = $this->pdo->prepare('SELECT id FROM profile_snapshots WHERE site_id = ? ORDER BY fetched_at DESC, id DESC LIMIT 1');
        $lastAttempt->execute([$id]);
        $active = $this->pdo->prepare("SELECT * FROM jobs WHERE site_id = ? AND type = 'profile' AND status IN ('queued','running') ORDER BY id DESC LIMIT 1");
        $active->execute([$id]);
        $activeRow = $active->fetch();

        $count = fn (string $sql) => (int) $this->scalar($sql, [$id]);
        return $item + [
            'registrableDomain' => $row['registrable_domain'],
            'finalUrl' => $row['final_url'],
            'title' => $row['title'],
            'registrar' => $row['registrar'],
            'ipAddress' => $row['ip_address'],
            'allowPrivateNet' => (bool) $row['allow_private_net'],
            'createdAt' => $row['created_at'],
            'latestSnapshot' => $snap($row['latest_snapshot_id'] === null ? null : (int) $row['latest_snapshot_id']),
            'lastAttempt' => $snap(($v = $lastAttempt->fetchColumn()) ? (int) $v : null),
            'activeJob' => $activeRow ? JobRepository::toApi($activeRow) : null,
            'contacts' => $this->all('SELECT id, name, role, email, phone, preferred_channel AS preferredChannel FROM contacts WHERE site_id = ? ORDER BY id', [$id]),
            'links' => $this->all('SELECT id, kind, label, url FROM site_links WHERE site_id = ? ORDER BY sort_order, id', [$id]),
            'counts' => [
                'notes' => $count('SELECT count(*) FROM notes WHERE site_id = ?'),
                'todosOpen' => $count('SELECT count(*) FROM todos WHERE site_id = ? AND done = 0'),
                'todosDone' => $count('SELECT count(*) FROM todos WHERE site_id = ? AND done = 1'),
                'crawls' => $count('SELECT count(*) FROM crawls WHERE site_id = ?'),
                'snapshots' => $count('SELECT count(*) FROM profile_snapshots WHERE site_id = ?'),
            ],
        ];
    }

    /** @return array<string,mixed>|null */
    public function snapshotMeta(int $snapshotId): ?array
    {
        $stmt = $this->pdo->prepare(
            "SELECT id, site_id, fetched_at, outcome, duration_ms, http_status, error_summary, changes_count, schema_version,
                    json_extract(summary_json, '$.cms.name') AS cms, json_extract(summary_json, '$.cms.version') AS cms_version,
                    json_extract(summary_json, '$.hosting.ips[0]') AS ip, json_extract(summary_json, '$.tls.validUntil') AS tls_until
             FROM profile_snapshots WHERE id = ?"
        );
        $stmt->execute([$snapshotId]);
        $r = $stmt->fetch();
        if (!$r) {
            return null;
        }
        return [
            'id' => (int) $r['id'], 'siteId' => (int) $r['site_id'], 'fetchedAt' => $r['fetched_at'], 'outcome' => $r['outcome'],
            'durationMs' => $r['duration_ms'] === null ? null : (int) $r['duration_ms'],
            'httpStatus' => $r['http_status'] === null ? null : (int) $r['http_status'],
            'errorSummary' => $r['error_summary'],
            'changesCount' => $r['changes_count'] === null ? null : (int) $r['changes_count'],
            'schemaVersion' => (int) $r['schema_version'],
            'cms' => $r['cms'], 'cmsVersion' => $r['cms_version'], 'ip' => $r['ip'],
            'tlsValidUntil' => $r['tls_until'] ? substr((string) $r['tls_until'], 0, 10) : null,
        ];
    }

    /** @param list<mixed> $args @return list<array<string,mixed>> */
    private function all(string $sql, array $args): array
    {
        $stmt = $this->pdo->prepare($sql);
        $stmt->execute($args);
        return $stmt->fetchAll();
    }

    /** @param list<mixed> $args */
    private function scalar(string $sql, array $args): mixed
    {
        $stmt = $this->pdo->prepare($sql);
        $stmt->execute($args);
        return $stmt->fetchColumn();
    }

    // ---- writing ---------------------------------------------------------------------------------------------

    /** @return array{0:string,1:array<string,mixed>} message for the 422 and the normalised URL parts */
    public function normalizeUrl(UrlNormalizer $normalizer, mixed $url): array
    {
        if (!is_string($url)) {
            throw new HttpException(422, 'validation_failed', 'A URL is required.', ['url' => 'Enter a website URL.']);
        }
        try {
            return ['', $normalizer->normalize($url)];
        } catch (InvalidArgumentException $e) {
            throw new HttpException(422, 'validation_failed', $e->getMessage(), ['url' => $e->getMessage()]);
        }
    }

    public function validateStatus(mixed $status): string
    {
        if (!is_string($status) || !in_array($status, SiteQuery::STATUSES, true)) {
            throw new HttpException(422, 'validation_failed', 'Invalid status.', ['status' => 'Use one of: ' . implode(', ', SiteQuery::STATUSES)]);
        }
        return $status;
    }

    public function validateCategory(mixed $id): ?int
    {
        if ($id === null || $id === '') {
            return null;
        }
        if (!is_numeric($id) || !$this->scalar('SELECT 1 FROM categories WHERE id = ?', [(int) $id])) {
            throw new HttpException(422, 'validation_failed', 'Unknown category.', ['categoryId' => 'Choose an existing category.']);
        }
        return (int) $id;
    }

    public function validatePreviewUrl(mixed $url): ?string
    {
        if ($url === null || trim((string) $url) === '') {
            return null;
        }
        $value = trim((string) $url);
        if (!preg_match('#^https?://#i', $value)) {
            $value = 'https://' . $value;
        }
        $parts = parse_url($value);
        if (!$parts || empty($parts['host']) || isset($parts['user']) || isset($parts['pass']) || strlen($value) > 500
            || !filter_var($value, FILTER_VALIDATE_URL)) {
            throw new HttpException(422, 'validation_failed', 'Invalid preview URL.', ['previewUrl' => 'Enter a full URL such as https://my-site.vercel.app']);
        }
        return $value;
    }

    public function validateDeadline(mixed $date): ?string
    {
        if ($date === null || $date === '') {
            return null;
        }
        if (!is_string($date) || !preg_match('/^\d{4}-\d{2}-\d{2}$/', $date) || !checkdate((int) substr($date, 5, 2), (int) substr($date, 8, 2), (int) substr($date, 0, 4))) {
            throw new HttpException(422, 'validation_failed', 'Invalid deadline.', ['deadline' => 'Use the format YYYY-MM-DD.']);
        }
        return $date;
    }

    /** @return list<string> unique, trimmed tag names */
    public function validateTags(mixed $tags): array
    {
        if (!is_array($tags)) {
            throw new HttpException(422, 'validation_failed', 'Tags must be a list.', ['tags' => 'Send an array of tag names.']);
        }
        $names = [];
        foreach ($tags as $tag) {
            $name = trim((string) $tag);
            if ($name === '') {
                continue;
            }
            if (mb_strlen($name) > 40) {
                throw new HttpException(422, 'validation_failed', 'Tag too long.', ['tags' => 'Tag names are at most 40 characters.']);
            }
            $names[mb_strtolower($name)] ??= $name;
        }
        return array_values($names);
    }

    /** Replaces the site's tags, creating missing ones. Returns [added names, removed names]. @param list<string> $names @return array{list<string>,list<string>} */
    public function setTags(int $siteId, array $names): array
    {
        $current = array_column($this->all('SELECT t.id, t.name FROM site_tags st JOIN tags t ON t.id = st.tag_id WHERE st.site_id = ?', [$siteId]), 'name', 'id');
        $wanted = [];
        foreach ($names as $name) {
            $id = $this->scalar('SELECT id FROM tags WHERE name = ? COLLATE NOCASE', [$name]);
            if (!$id) {
                $this->pdo->prepare('INSERT INTO tags (name) VALUES (?)')->execute([$name]);
                $id = $this->pdo->lastInsertId();
            }
            $wanted[(int) $id] = $name;
        }
        $added = [];
        foreach (array_diff_key($wanted, $current) as $id => $name) {
            $this->pdo->prepare('INSERT INTO site_tags (site_id, tag_id) VALUES (?, ?)')->execute([$siteId, $id]);
            $added[] = $name;
        }
        $removed = [];
        foreach (array_diff_key($current, $wanted) as $id => $name) {
            $this->pdo->prepare('DELETE FROM site_tags WHERE site_id = ? AND tag_id = ?')->execute([$siteId, $id]);
            $removed[] = $name;
        }
        return [$added, $removed];
    }

    public function uniqueKey(string $key): string
    {
        $candidate = $key;
        for ($n = 2; $this->scalar('SELECT 1 FROM sites WHERE site_key = ?', [$candidate]); $n++) {
            $candidate = "$key#$n";
        }
        return $candidate;
    }

    /** @return array{id:int,siteKey:string}|null */
    public function findByKey(string $key): ?array
    {
        $stmt = $this->pdo->prepare('SELECT id, site_key FROM sites WHERE site_key = ?');
        $stmt->execute([$key]);
        $r = $stmt->fetch();
        return $r ? ['id' => (int) $r['id'], 'siteKey' => $r['site_key']] : null;
    }

    /** @return list<array{id:int,siteKey:string}> same registrable domain, other host */
    public function related(?string $registrable, string $exceptKey): array
    {
        if ($registrable === null) {
            return [];
        }
        return array_map(
            static fn ($r) => ['id' => (int) $r['id'], 'siteKey' => $r['site_key']],
            $this->all('SELECT id, site_key FROM sites WHERE registrable_domain = ? AND site_key != ? LIMIT 5', [$registrable, $exceptKey])
        );
    }

    public function guard(): NetworkGuard
    {
        return new NetworkGuard();
    }
}
