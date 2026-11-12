<?php
declare(strict_types=1);

namespace App\Tests\Unit;

use App\Infra\Backup;
use App\Infra\Database;
use App\Infra\Migrator;
use App\Infra\Paths;
use App\Tests\TempDir;
use PHPUnit\Framework\TestCase;

final class MigratorTest extends TestCase
{
    private string $dir;

    protected function setUp(): void
    {
        $this->dir = TempDir::create();
    }

    protected function tearDown(): void
    {
        TempDir::remove($this->dir);
    }

    public function testAppliesInitialSchemaOnce(): void
    {
        $paths = new Paths($this->dir);
        $pdo = Database::open($paths);
        $migrator = new Migrator($pdo, dirname(__DIR__, 2) . '/migrations', new Backup($pdo, $paths));

        $this->assertSame([1, 2], $migrator->migrate());
        $this->assertSame([], $migrator->migrate());
        $this->assertSame(['current' => 2, 'latest' => 2, 'pending' => []], $migrator->status());
        $tables = $pdo->query("SELECT name FROM sqlite_master WHERE type='table'")->fetchAll(\PDO::FETCH_COLUMN);
        foreach (['sites', 'jobs', 'profile_snapshots', 'notes', 'todos', 'crawls', 'saved_views'] as $table) {
            $this->assertContains($table, $tables);
        }
        $this->assertSame(1, (int) $pdo->query('PRAGMA foreign_keys')->fetchColumn());
        $this->assertSame('wal', $pdo->query('PRAGMA journal_mode')->fetchColumn());
    }

    public function testBacksUpBeforeLaterMigrationsAndRollsBackFailures(): void
    {
        $paths = new Paths($this->dir);
        $pdo = Database::open($paths);
        $migDir = $this->dir . '/migrations';
        mkdir($migDir);
        file_put_contents($migDir . '/0001_a.sql', 'CREATE TABLE a (id INTEGER);');
        $migrator = new Migrator($pdo, $migDir, new Backup($pdo, $paths));
        $migrator->migrate();
        $this->assertSame([], glob($this->dir . '/backups/*') ?: [], 'no backup for a fresh database');

        file_put_contents($migDir . '/0002_bad.sql', 'CREATE TABLE b (id INTEGER); INSERT INTO missing VALUES (1);');
        try {
            $migrator->migrate();
            $this->fail('expected failure');
        } catch (\RuntimeException $e) {
            $this->assertStringContainsString('Migration 2 failed', $e->getMessage());
        }
        $this->assertCount(1, glob($this->dir . '/backups/*pre-migration-0002*') ?: []);
        $this->assertSame(1, $migrator->status()['current']);
        $this->assertFalse((bool) $pdo->query("SELECT 1 FROM sqlite_master WHERE name='b'")->fetchColumn(), 'failed migration rolled back');
    }
}
