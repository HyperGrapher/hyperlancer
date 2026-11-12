<?php
declare(strict_types=1);

namespace App\Jobs;

use App\Infra\Paths;
use PDO;

/** Housekeeping for finished jobs (docs/PRD.md §9.2: job folders are pruned after 30 days; crawl output never). */
final class JobMaintenance
{
    public function __construct(
        private readonly JobRepository $jobs,
        private readonly Paths $paths,
        private readonly int $keepDays = 30,
    ) {
    }

    /** Removes data/jobs/<id>/ for jobs finished long ago and clears their log path. Returns the folders removed. */
    public function pruneFinished(): int
    {
        $pdo = $this->jobs->pdo();
        $stmt = $pdo->prepare(
            "SELECT id FROM jobs WHERE status NOT IN ('queued','running') AND log_path IS NOT NULL
               AND finished_at IS NOT NULL AND finished_at < strftime('%Y-%m-%dT%H:%M:%fZ','now', ?)"
        );
        $stmt->execute(["-{$this->keepDays} days"]);
        $removed = 0;
        foreach ($stmt->fetchAll(PDO::FETCH_COLUMN) as $id) {
            $dir = $this->paths->data('jobs', (string) $id);
            if (is_dir($dir)) {
                $this->removeTree($dir);
                $removed++;
            }
            $pdo->prepare('UPDATE jobs SET log_path = NULL WHERE id = ?')->execute([$id]);
        }
        return $removed;
    }

    private function removeTree(string $dir): void
    {
        $items = new \RecursiveIteratorIterator(
            new \RecursiveDirectoryIterator($dir, \FilesystemIterator::SKIP_DOTS),
            \RecursiveIteratorIterator::CHILD_FIRST,
        );
        foreach ($items as $item) {
            $item->isDir() ? @rmdir($item->getPathname()) : @unlink($item->getPathname());
        }
        @rmdir($dir);
    }
}
