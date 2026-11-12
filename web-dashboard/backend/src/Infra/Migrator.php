<?php
declare(strict_types=1);

namespace App\Infra;

use PDO;
use RuntimeException;

/**
 * Numbered .sql files applied in order inside a transaction, recorded in schema_migrations.
 * A backup of the database is taken first whenever something is pending on an existing database.
 */
final class Migrator
{
    public function __construct(
        private readonly PDO $pdo,
        private readonly string $dir,
        private readonly ?Backup $backup = null,
    ) {
    }

    /** @return array<int,string> version => file path */
    public function available(): array
    {
        $files = [];
        foreach (glob($this->dir . DIRECTORY_SEPARATOR . '[0-9][0-9][0-9][0-9]_*.sql') ?: [] as $file) {
            $files[(int) basename($file)] = $file;
        }
        ksort($files);
        return $files;
    }

    /** @return list<int> */
    public function applied(): array
    {
        $exists = $this->pdo->query("SELECT 1 FROM sqlite_master WHERE type='table' AND name='schema_migrations'")->fetchColumn();
        if (!$exists) {
            return [];
        }
        return array_map('intval', $this->pdo->query('SELECT version FROM schema_migrations ORDER BY version')->fetchAll(PDO::FETCH_COLUMN));
    }

    /** @return array{current:int,latest:int,pending:list<int>} */
    public function status(): array
    {
        $applied = $this->applied();
        $available = array_keys($this->available());
        return [
            'current' => $applied ? max($applied) : 0,
            'latest' => $available ? max($available) : 0,
            'pending' => array_values(array_diff($available, $applied)),
        ];
    }

    /** @return list<int> versions applied now */
    public function migrate(): array
    {
        $status = $this->status();
        if (!$status['pending']) {
            return [];
        }
        $this->pdo->exec(
            "CREATE TABLE IF NOT EXISTS schema_migrations (
               version INTEGER PRIMARY KEY, name TEXT NOT NULL,
               applied_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')))"
        );
        if ($status['current'] > 0 && $this->backup) {
            $this->backup->create('pre-migration-' . sprintf('%04d', max($status['pending'])));
        }
        $files = $this->available();
        $done = [];
        foreach ($status['pending'] as $version) {
            $sql = file_get_contents($files[$version]);
            if ($sql === false) {
                throw new RuntimeException("Cannot read migration {$files[$version]}");
            }
            $this->pdo->exec('BEGIN IMMEDIATE');
            try {
                // Another process may have applied it while we waited for the write lock.
                if (in_array($version, $this->applied(), true)) {
                    $this->pdo->exec('ROLLBACK');
                    continue;
                }
                $this->pdo->exec($sql);
                $name = preg_replace('/^\d+_|\.sql$/', '', basename($files[$version]));
                $stmt = $this->pdo->prepare('INSERT INTO schema_migrations (version, name) VALUES (?, ?)');
                $stmt->execute([$version, $name]);
                $this->pdo->exec('COMMIT');
                $done[] = $version;
            } catch (\Throwable $e) {
                // PDO's inTransaction() does not see a manual BEGIN, so roll back unconditionally.
                try {
                    $this->pdo->exec('ROLLBACK');
                } catch (\Throwable) {
                }
                throw new RuntimeException("Migration $version failed: " . $e->getMessage(), 0, $e);
            }
        }
        return $done;
    }
}
