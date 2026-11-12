<?php
declare(strict_types=1);

namespace App\Domain;

use App\Http\HttpException;
use App\Infra\Paths;
use App\Jobs\JobRepository;
use App\Security\NetworkGuard;
use PDO;

/** The `crawls` table: creating runs, reading them back for the API, and turning a finished job into counts. */
final class CrawlStore
{
    private const DEFAULTS = [
        'maxPages' => 200, 'maxDepth' => 5, 'includeSubdomains' => false, 'include' => [], 'exclude' => [], 'respectRobots' => true,
        'delay' => 0.5, 'concurrency' => 1, 'types' => ['images' => true, 'documents' => true, 'media' => false, 'fonts' => false],
        'maxFileMb' => 25, 'maxTotalMb' => 1024, 'strip' => [], 'keepChrome' => false, 'languages' => [], 'wordpress' => 'auto',
    ];

    public function __construct(private readonly PDO $pdo, private readonly Paths $paths, private readonly JobRepository $jobs)
    {
    }

    /** @return array<string,mixed> normalised options as stored (and as the dialog prefills them) */
    public function validateOptions(array $in, string $siteUrl): array
    {
        $errors = [];
        $int = function (string $key, int $min, int $max) use ($in, &$errors): int {
            $v = $in[$key] ?? self::DEFAULTS[$key];
            if (!is_numeric($v) || (int) $v != $v || $v < $min || $v > $max) {
                $errors[$key] = "Must be a whole number from $min to $max.";
                return (int) self::DEFAULTS[$key];
            }
            return (int) $v;
        };
        $num = function (string $key, float $min, float $max) use ($in, &$errors): float {
            $v = $in[$key] ?? self::DEFAULTS[$key];
            if (!is_numeric($v) || $v < $min || $v > $max) {
                $errors[$key] = "Must be between $min and $max.";
                return (float) self::DEFAULTS[$key];
            }
            return (float) $v;
        };
        $list = function (string $key, int $max) use ($in, &$errors): array {
            $v = $in[$key] ?? [];
            if (!is_array($v) || count($v) > $max) {
                $errors[$key] = "At most $max entries.";
                return [];
            }
            return array_values(array_filter(array_map(static fn ($x) => trim((string) $x), $v), static fn ($x) => $x !== ''));
        };

        $opts = [
            'maxPages' => $int('maxPages', 1, 5000),
            'maxDepth' => $int('maxDepth', 0, 20),
            'includeSubdomains' => (bool) ($in['includeSubdomains'] ?? false),
            'include' => $list('include', 30),
            'exclude' => $list('exclude', 30),
            'respectRobots' => (bool) ($in['respectRobots'] ?? true),
            'delay' => $num('delay', 0, 30),
            'concurrency' => $int('concurrency', 1, 4),
            'maxFileMb' => $num('maxFileMb', 1, 500),
            'maxTotalMb' => $num('maxTotalMb', 1, 20000),
            'strip' => $list('strip', 30),
            'keepChrome' => (bool) ($in['keepChrome'] ?? false),
            'languages' => $list('languages', 20),
            'wordpress' => in_array($in['wordpress'] ?? 'auto', ['auto', 'off', 'only'], true) ? $in['wordpress'] ?? 'auto' : 'auto',
        ];
        $types = is_array($in['types'] ?? null) ? $in['types'] : [];
        $opts['types'] = [];
        foreach (self::DEFAULTS['types'] as $k => $d) {
            $opts['types'][$k] = array_key_exists($k, $types) ? (bool) $types[$k] : $d;
        }
        if (!empty($in['extensions']) && is_array($in['extensions'])) {
            $opts['extensions'] = array_values(array_filter(array_map(static fn ($e) => strtolower(ltrim(trim((string) $e), '.')), $in['extensions']), static fn ($e) => preg_match('/^[a-z0-9]{1,8}$/', $e)));
        }
        $main = trim((string) ($in['mainSelector'] ?? ''));
        if ($main !== '') {
            $opts['mainSelector'] = mb_substr($main, 0, 200);
        }
        foreach (['include', 'exclude'] as $key) {
            foreach ($opts[$key] as $i => $pattern) {
                if (@preg_match('~' . str_replace('~', '\~', $pattern) . '~u', '') === false) {
                    $errors["$key.$i"] = "Invalid pattern: $pattern";
                }
            }
        }
        if (!$opts['respectRobots'] && empty($in['confirmOwnership'])) {
            $errors['respectRobots'] = 'Confirm that you own or may copy this site to ignore robots.txt.';
        }
        $urls = $in['startUrls'] ?? [$siteUrl];
        $opts['startUrls'] = [];
        foreach (is_array($urls) ? $urls : [] as $u) {
            $u = trim((string) $u);
            $parts = parse_url($u);
            if ($u === '' || !in_array($parts['scheme'] ?? '', ['http', 'https'], true) || empty($parts['host'])) {
                $errors['startUrls'] = 'Start URLs must be http(s) addresses.';
                break;
            }
            $opts['startUrls'][] = $u;
        }
        if (!$opts['startUrls'] && !isset($errors['startUrls'])) {
            $opts['startUrls'] = [$siteUrl];
        }
        if (count($opts['startUrls']) > 20) {
            $errors['startUrls'] = 'At most 20 start URLs.';
        }
        if ($errors) {
            throw new HttpException(422, 'validation_failed', 'Some crawl options are invalid.', $errors);
        }
        return $opts;
    }

    /** @param array<string,mixed> $site raw sites row @param array<string,mixed> $opts @return array<string,mixed> */
    public function create(array $site, array $opts): array
    {
        $guard = new NetworkGuard();
        $allowPrivate = (bool) $site['allow_private_net'];
        foreach ($opts['startUrls'] as $u) {
            if ($guard->blockedReason((string) parse_url($u, PHP_URL_HOST), $allowPrivate) !== null) {
                throw new HttpException(422, 'blocked', 'That address is on a private network.', ['startUrls' => 'Enable "Allow local/LAN target" on the site first.']);
            }
        }
        if ($this->activeCount() > 0) {
            throw new HttpException(429, 'busy', 'Another crawl is already queued or running.');
        }
        $slug = preg_replace('/[^A-Za-z0-9._-]+/', '_', (string) $site['site_key']) ?: 'site';
        $stamp = gmdate('Ymd-His');
        $rel = "crawls/$slug/$stamp";
        for ($n = 2; is_dir($this->paths->fromRelative($rel)); $n++) {
            $rel = "crawls/$slug/$stamp-$n";
        }
        $abs = $this->paths->ensureDir(...explode('/', $rel));
        $job = $this->jobs->enqueue('crawl', (int) $site['id'], $opts + ['outputDir' => $abs, 'allowPrivate' => $allowPrivate]);
        $this->pdo->prepare("INSERT INTO crawls (site_id, job_id, output_dir, options_json, status, started_at) VALUES (?, ?, ?, ?, 'running', strftime('%Y-%m-%dT%H:%M:%fZ','now'))")
            ->execute([(int) $site['id'], $job['id'], $rel, json_encode($opts, JSON_UNESCAPED_SLASHES)]);
        return $this->get((int) $this->pdo->lastInsertId());
    }

    public function activeCount(): int
    {
        return (int) $this->pdo->query(
            "SELECT count(*) FROM crawls c LEFT JOIN jobs j ON j.id = c.job_id
             WHERE c.status = 'running' AND (j.id IS NULL OR j.status IN ('queued','running'))"
        )->fetchColumn();
    }

    /** @return array<string,mixed>|null */
    public function row(int $id): ?array
    {
        $stmt = $this->pdo->prepare('SELECT * FROM crawls WHERE id = ?');
        $stmt->execute([$id]);
        return $stmt->fetch() ?: null;
    }

    /** @return array<string,mixed> */
    public function require(int $id): array
    {
        return $this->row($id) ?? throw new HttpException(404, 'not_found', 'Crawl not found.');
    }

    public function absDir(array $row): string
    {
        return $this->paths->fromRelative((string) $row['output_dir']);
    }

    /** @return array<string,mixed> */
    public function get(int $id): array
    {
        return $this->toApi($this->require($id));
    }

    /** @return list<array<string,mixed>> */
    public function forSite(int $siteId): array
    {
        $stmt = $this->pdo->prepare('SELECT * FROM crawls WHERE site_id = ? ORDER BY started_at DESC, id DESC');
        $stmt->execute([$siteId]);
        return array_map(fn ($r) => $this->toApi($r), $stmt->fetchAll());
    }

    /** @param array<string,mixed> $row @return array<string,mixed> */
    public function toApi(array $row): array
    {
        $job = $row['job_id'] === null ? null : $this->jobs->find((int) $row['job_id']);
        $status = $row['status'];
        if ($status === 'running' && $job) {
            $status = match ($job['status']) {
                'queued' => 'queued',
                'running' => $job['cancelRequested'] ? 'canceling' : 'running',
                'canceled' => 'canceled',
                // The worker marks the job finished a moment before the hook fills in the crawl row; after
                // ten seconds without it, ingesting evidently failed.
                default => time() - (int) strtotime((string) $job['finishedAt']) < 10 ? 'running' : 'failed',
            };
        }
        $running = in_array($status, ['queued', 'running', 'canceling'], true);
        return [
            'id' => (int) $row['id'],
            'siteId' => (int) $row['site_id'],
            'jobId' => $row['job_id'] === null ? null : (int) $row['job_id'],
            'status' => $status,
            'outputDir' => $row['output_dir'],
            'path' => $this->absDir($row),
            'options' => json_decode((string) $row['options_json'], true),
            'pages' => (int) $row['pages_count'],
            'images' => (int) $row['images_count'],
            'files' => (int) $row['files_count'],
            'bytes' => (int) $row['bytes_total'],
            'archiveReady' => $row['archive_path'] !== null && is_file($this->paths->fromRelative((string) $row['archive_path'])),
            'startedAt' => $row['started_at'],
            'finishedAt' => $row['finished_at'],
            'stopReason' => $this->stopReason($row),
            'job' => $running ? $job : null,
        ];
    }

    private function stopReason(array $row): ?string
    {
        if ($row['status'] === 'running') {
            return null;
        }
        $m = (new CrawlReader($this->absDir($row)))->manifest();
        return is_array($m) ? ($m['stop_reason'] ?? null) : null;
    }

    /** Worker hook: copy the final counts and status from manifest.json (or the job's failure) into the row. */
    public function ingest(array $job, string $status): void
    {
        $stmt = $this->pdo->prepare('SELECT * FROM crawls WHERE job_id = ?');
        $stmt->execute([(int) $job['id']]);
        $row = $stmt->fetch();
        if (!$row) {
            return;
        }
        $manifest = (new CrawlReader($this->absDir($row)))->manifest();
        $counts = is_array($manifest) ? ($manifest['counts'] ?? []) : [];
        $final = in_array($manifest['status'] ?? '', ['done', 'partial', 'failed', 'canceled'], true) ? $manifest['status'] : null;
        $final ??= match ($status) {
            'succeeded' => 'done', 'partial' => 'partial', 'canceled' => 'canceled', default => 'failed',
        };
        if ($status === 'canceled') {
            $final = 'canceled';
        } elseif ($status === 'failed' && $final !== 'failed') {
            $final = $counts ? 'partial' : 'failed'; // killed or timed out: keep whatever was indexed
        }
        $this->pdo->prepare(
            "UPDATE crawls SET status = ?, pages_count = ?, images_count = ?, files_count = ?, bytes_total = ?,
                    finished_at = strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id = ?"
        )->execute([$final, (int) ($counts['pages'] ?? 0), (int) ($counts['images'] ?? 0), (int) ($counts['files'] ?? 0), (int) ($counts['bytes'] ?? 0), (int) $row['id']]);
        (new ActivityLogger($this->pdo))->log((int) $row['site_id'], "crawl." . ($final === 'done' || $final === 'partial' ? 'finished' : $final),
            sprintf('Crawl %s: %d pages, %d images', $final, $counts['pages'] ?? 0, $counts['images'] ?? 0), ['crawlId' => (int) $row['id']]);
    }

    /** Worker hook for `archive` jobs: remember the ZIP once it exists. */
    public function ingestArchive(array $job, string $status): void
    {
        $params = json_decode((string) $job['params_json'], true) ?: [];
        $crawlId = (int) ($params['crawlId'] ?? 0);
        $rel = (string) ($params['archive'] ?? '');
        if ($crawlId > 0 && $rel !== '' && in_array($status, ['succeeded', 'partial'], true) && is_file($this->paths->fromRelative($rel))) {
            $this->pdo->prepare('UPDATE crawls SET archive_path = ? WHERE id = ?')->execute([$rel, $crawlId]);
        }
    }

    public function delete(array $row): void
    {
        $this->pdo->prepare('DELETE FROM crawls WHERE id = ?')->execute([(int) $row['id']]);
        if ($row['archive_path'] !== null) {
            @unlink($this->paths->fromRelative((string) $row['archive_path']));
        }
        CrawlReader::removeTree($this->absDir($row));
    }

    public function deleteForSite(int $siteId): void
    {
        $stmt = $this->pdo->prepare('SELECT * FROM crawls WHERE site_id = ?');
        $stmt->execute([$siteId]);
        foreach ($stmt->fetchAll() as $row) {
            $this->delete($row);
        }
    }
}
