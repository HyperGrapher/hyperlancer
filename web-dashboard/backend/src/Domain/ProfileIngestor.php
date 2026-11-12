<?php
declare(strict_types=1);

namespace App\Domain;

use PDO;

/** Stores a profile report as a snapshot and copies its facts onto the site (docs/DECISIONS.md D3). */
final class ProfileIngestor
{
    public function __construct(
        private readonly PDO $pdo,
        private readonly ProfileNormalizer $normalizer,
        private readonly SnapshotDiff $diff,
        private readonly ActivityLogger $activity,
    ) {
    }

    /**
     * @param array<string,mixed> $raw decoded profile.json
     * @return array{snapshotId:int,outcome:string,changes:?int}
     */
    public function ingest(int $siteId, ?int $jobId, array $raw, ?string $rawJson = null): array
    {
        return $this->store($siteId, $jobId, $raw, $this->normalizer->normalize($raw), $rawJson);
    }

    /** An attempt that produced no readable report (crash, timeout, unreadable file). */
    public function ingestFailure(int $siteId, ?int $jobId, string $message): array
    {
        $raw = ['schema_version' => 2, 'status' => 'failed', 'errors' => [['section' => 'job', 'code' => 'crash', 'message' => $message]],
            'metadata' => ['scanned_at_utc' => gmdate('Y-m-d\TH:i:s\Z'), 'duration_ms' => null]];
        return $this->store($siteId, $jobId, $raw, $this->normalizer->failed($message), null);
    }

    /** @param array<string,mixed> $raw @param array<string,mixed> $summary */
    private function store(int $siteId, ?int $jobId, array $raw, array $summary, ?string $rawJson): array
    {
        $outcome = $summary['outcome'];
        $site = $this->pdo->prepare('SELECT id, url FROM sites WHERE id = ?');
        $site->execute([$siteId]);
        $siteRow = $site->fetch();
        if (!$siteRow) {
            throw new \RuntimeException("Site $siteId no longer exists.");
        }

        // Changes are counted against the newest snapshot that actually has data.
        $prev = $this->pdo->prepare("SELECT id, summary_json FROM profile_snapshots WHERE site_id = ? AND outcome != 'failed' ORDER BY fetched_at DESC, id DESC LIMIT 1");
        $prev->execute([$siteId]);
        $previous = $prev->fetch();
        $changes = null;
        $diffResult = null;
        if ($outcome !== 'failed' && $previous) {
            $diffResult = $this->diff->diff(json_decode($previous['summary_json'], true) ?: [], $summary);
            $changes = $diffResult['count'];
        }

        $first = $summary['errors'][0]['message'] ?? null;
        $fetchedAt = (string) ($raw['metadata']['scanned_at_utc'] ?? gmdate('Y-m-d\TH:i:s\Z'));
        $this->pdo->beginTransaction();
        try {
            $this->pdo->prepare(
                'INSERT INTO profile_snapshots (site_id, job_id, fetched_at, outcome, schema_version, tool_version, requested_url, final_url,
                    http_status, duration_ms, error_summary, changes_count, summary_json, raw_json)
                 VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)'
            )->execute([
                $siteId, $jobId, $fetchedAt, $outcome, $summary['schemaVersion'], $raw['metadata']['version'] ?? null,
                $summary['url']['requested'] ?? $siteRow['url'], $summary['url']['final'], $summary['http']['status'],
                $raw['metadata']['duration_ms'] ?? null, $first, $changes,
                json_encode($summary, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES | JSON_INVALID_UTF8_SUBSTITUTE | JSON_PARTIAL_OUTPUT_ON_ERROR),
                $rawJson ?? json_encode($raw, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES | JSON_INVALID_UTF8_SUBSTITUTE | JSON_PARTIAL_OUTPUT_ON_ERROR),
            ]);
            $snapshotId = (int) $this->pdo->lastInsertId();

            $tech = $this->pdo->prepare('INSERT OR IGNORE INTO snapshot_technologies (snapshot_id, category, name, version, confidence) VALUES (?, ?, ?, ?, ?)');
            foreach ($summary['tech'] as $t) {
                $tech->execute([$snapshotId, $t['category'], $t['name'], $t['version'], $t['confidence']]);
            }

            $this->updateSite($siteId, $summary, $outcome, $fetchedAt);
            $this->pdo->commit();
        } catch (\Throwable $e) {
            $this->pdo->rollBack();
            throw $e;
        }

        $message = match ($outcome) {
            'failed' => 'Profile fetch failed' . ($first ? ": $first" : ''),
            default => 'Profile fetched (' . $outcome . ')' . ($changes !== null ? ($changes === 0 ? ', no changes' : ", $changes change" . ($changes === 1 ? '' : 's')) : ''),
        };
        $this->activity->log($siteId, 'profile.fetched', $message, ['snapshotId' => $snapshotId, 'outcome' => $outcome, 'changes' => $changes]);
        return ['snapshotId' => $snapshotId, 'outcome' => $outcome, 'changes' => $changes];
    }

    /**
     * latest_snapshot_id points to the newest snapshot that has data (so a failed attempt does not blank the
     * profile); last_fetched_at and last_fetch_outcome always describe the newest attempt.
     *
     * @param array<string,mixed> $summary
     */
    private function updateSite(int $siteId, array $summary, string $outcome, string $fetchedAt): void
    {
        $now = "strftime('%Y-%m-%dT%H:%M:%fZ','now')";
        if ($outcome === 'failed') {
            $this->pdo->prepare("UPDATE sites SET last_fetched_at = ?, last_fetch_outcome = 'failed', updated_at = $now WHERE id = ?")->execute([$fetchedAt, $siteId]);
            $has = $this->pdo->prepare('SELECT latest_snapshot_id FROM sites WHERE id = ?');
            $has->execute([$siteId]);
            if (!$has->fetchColumn()) { // nothing better to show yet
                $this->pdo->prepare('UPDATE sites SET latest_snapshot_id = (SELECT max(id) FROM profile_snapshots WHERE site_id = ?) WHERE id = ?')->execute([$siteId, $siteId]);
            }
            return;
        }
        $facts = $this->normalizer->facts($summary);
        $set = implode(', ', array_map(static fn ($c) => "$c = :$c", array_keys($facts)));
        $stmt = $this->pdo->prepare(
            "UPDATE sites SET $set, latest_snapshot_id = (SELECT max(id) FROM profile_snapshots WHERE site_id = :site AND outcome != 'failed'),
                last_fetched_at = :fetched, last_fetch_outcome = :outcome, updated_at = $now WHERE id = :site"
        );
        $stmt->execute($facts + ['site' => $siteId, 'fetched' => $fetchedAt, 'outcome' => $outcome]);
    }
}
