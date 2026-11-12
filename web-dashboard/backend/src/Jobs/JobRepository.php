<?php
declare(strict_types=1);

namespace App\Jobs;

use PDO;

final class JobRepository
{
    public const TYPES = ['profile', 'crawl', 'archive', 'backup', 'selfcheck'];

    /** Concurrent running jobs per type (docs/PRD.md §9.2). */
    public const LIMITS = ['profile' => 3, 'crawl' => 1, 'archive' => 1, 'backup' => 1, 'selfcheck' => 3];

    public function __construct(private readonly PDO $pdo)
    {
    }

    public function pdo(): PDO
    {
        return $this->pdo;
    }

    /** @param array<string,mixed> $params @return array<string,mixed> */
    public function enqueue(string $type, ?int $siteId = null, array $params = []): array
    {
        $stmt = $this->pdo->prepare('INSERT INTO jobs (type, site_id, params_json) VALUES (?, ?, ?)');
        $stmt->execute([$type, $siteId, json_encode($params, JSON_UNESCAPED_SLASHES) ?: '{}']);
        $id = (int) $this->pdo->lastInsertId();
        $this->pdo->prepare('UPDATE jobs SET log_path = ? WHERE id = ?')->execute(["jobs/$id/job.log", $id]);
        return $this->find($id);
    }

    /** @return array<string,mixed>|null raw row */
    public function row(int $id): ?array
    {
        $stmt = $this->pdo->prepare('SELECT j.*, s.site_key FROM jobs j LEFT JOIN sites s ON s.id = j.site_id WHERE j.id = ?');
        $stmt->execute([$id]);
        return $stmt->fetch() ?: null;
    }

    /** @return array<string,mixed>|null API shape */
    public function find(int $id): ?array
    {
        $row = $this->row($id);
        return $row ? self::toApi($row) : null;
    }

    /** @return array{items:list<array<string,mixed>>,total:int,page:int,perPage:int} */
    public function list(string $status = 'all', ?string $type = null, ?int $siteId = null, int $page = 1, int $perPage = 50): array
    {
        $where = [];
        $args = [];
        if ($status === 'active') {
            $where[] = "j.status IN ('queued','running')";
        } elseif ($status === 'finished') {
            $where[] = "j.status NOT IN ('queued','running')";
        }
        if ($type !== null && in_array($type, self::TYPES, true)) {
            $where[] = 'j.type = ?';
            $args[] = $type;
        }
        if ($siteId !== null) {
            $where[] = 'j.site_id = ?';
            $args[] = $siteId;
        }
        $sqlWhere = $where ? 'WHERE ' . implode(' AND ', $where) : '';
        $total = $this->pdo->prepare("SELECT count(*) FROM jobs j $sqlWhere");
        $total->execute($args);
        $page = max(1, $page);
        $perPage = min(200, max(1, $perPage));
        $stmt = $this->pdo->prepare("SELECT j.*, s.site_key FROM jobs j LEFT JOIN sites s ON s.id = j.site_id $sqlWhere ORDER BY j.id DESC LIMIT $perPage OFFSET " . ($page - 1) * $perPage);
        $stmt->execute($args);
        return [
            'items' => array_map([self::class, 'toApi'], $stmt->fetchAll()),
            'total' => (int) $total->fetchColumn(),
            'page' => $page,
            'perPage' => $perPage,
        ];
    }

    /** Atomically moves the oldest queued job of $type to running if the type's limit allows. @return array<string,mixed>|null */
    public function claim(string $type, int $workerPid): ?array
    {
        $limit = self::LIMITS[$type] ?? 1;
        $stmt = $this->pdo->prepare(
            "UPDATE jobs SET status='running', started_at=strftime('%Y-%m-%dT%H:%M:%fZ','now'),
                    heartbeat_at=strftime('%Y-%m-%dT%H:%M:%fZ','now'), worker_pid=?, attempts=attempts+1
             WHERE id = (SELECT id FROM jobs WHERE status='queued' AND type=? ORDER BY id LIMIT 1)
               AND (SELECT count(*) FROM jobs WHERE status='running' AND type=?) < ?
             RETURNING *"
        );
        $stmt->execute([$workerPid, $type, $type, $limit]);
        return $stmt->fetch() ?: null;
    }

    public function queuedCount(): int
    {
        return (int) $this->pdo->query("SELECT count(*) FROM jobs WHERE status='queued'")->fetchColumn();
    }

    public function setChildPid(int $id, int $pid): void
    {
        $this->pdo->prepare('UPDATE jobs SET child_pid = ? WHERE id = ?')->execute([$pid, $id]);
    }

    public function heartbeat(int $id): void
    {
        $this->pdo->prepare("UPDATE jobs SET heartbeat_at = strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id = ?")->execute([$id]);
    }

    public function progress(int $id, ?int $current, ?int $total, ?string $label): void
    {
        $this->pdo->prepare('UPDATE jobs SET progress_current = ?, progress_total = ?, progress_label = ? WHERE id = ?')
            ->execute([$current, $total, $label, $id]);
    }

    /** @param array<string,mixed> $stats */
    public function stats(int $id, array $stats): void
    {
        $this->pdo->prepare('UPDATE jobs SET stats_json = ? WHERE id = ?')->execute([json_encode($stats), $id]);
    }

    public function setError(int $id, ?string $error): void
    {
        $this->pdo->prepare('UPDATE jobs SET error = ? WHERE id = ?')->execute([$error, $id]);
    }

    public function cancelRequested(int $id): bool
    {
        $stmt = $this->pdo->prepare('SELECT cancel_requested FROM jobs WHERE id = ?');
        $stmt->execute([$id]);
        return (bool) $stmt->fetchColumn();
    }

    /** Queued jobs are canceled at once; running ones are flagged for the worker. @return array<string,mixed>|null */
    public function requestCancel(int $id): ?array
    {
        $this->pdo->prepare(
            "UPDATE jobs SET cancel_requested = 1,
                    status = CASE WHEN status='queued' THEN 'canceled' ELSE status END,
                    finished_at = CASE WHEN status='queued' THEN strftime('%Y-%m-%dT%H:%M:%fZ','now') ELSE finished_at END
             WHERE id = ? AND status IN ('queued','running')"
        )->execute([$id]);
        return $this->find($id);
    }

    /** @param mixed $result */
    public function finish(int $id, string $status, ?int $exitCode = null, ?string $error = null, mixed $result = null): void
    {
        $this->pdo->prepare(
            "UPDATE jobs SET status = ?, exit_code = ?, error = ?, result_json = ?, child_pid = NULL,
                    finished_at = strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id = ?"
        )->execute([$status, $exitCode, $error, $result === null ? null : json_encode($result), $id]);
    }

    /** Marks running jobs whose heartbeat went stale as failed. Call only when no worker is alive. */
    public function failOrphans(int $staleSeconds = 30): int
    {
        $stmt = $this->pdo->prepare(
            "UPDATE jobs SET status='failed', error='Worker stopped unexpectedly', child_pid=NULL,
                    finished_at = strftime('%Y-%m-%dT%H:%M:%fZ','now')
             WHERE status='running' AND (heartbeat_at IS NULL OR heartbeat_at < strftime('%Y-%m-%dT%H:%M:%fZ','now', ?))"
        );
        $stmt->execute(["-$staleSeconds seconds"]);
        return $stmt->rowCount();
    }

    /** @param array<string,mixed> $row @return array<string,mixed> */
    public static function toApi(array $row): array
    {
        $json = static fn (?string $v) => $v === null ? null : json_decode($v, true);
        return [
            'id' => (int) $row['id'],
            'type' => $row['type'],
            'siteId' => $row['site_id'] === null ? null : (int) $row['site_id'],
            'siteKey' => $row['site_key'] ?? null,
            'status' => $row['status'],
            'progress' => [
                'current' => $row['progress_current'] === null ? null : (int) $row['progress_current'],
                'total' => $row['progress_total'] === null ? null : (int) $row['progress_total'],
                'label' => $row['progress_label'],
            ],
            'stats' => $json($row['stats_json']),
            'error' => $row['error'],
            'cancelRequested' => (bool) $row['cancel_requested'],
            'createdAt' => $row['created_at'],
            'startedAt' => $row['started_at'],
            'finishedAt' => $row['finished_at'],
            'result' => $json($row['result_json']),
        ];
    }
}
