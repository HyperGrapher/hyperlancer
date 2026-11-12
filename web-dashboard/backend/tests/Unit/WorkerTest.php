<?php
declare(strict_types=1);

namespace App\Tests\Unit;

use App\Infra\Config;
use App\Infra\Database;
use App\Infra\Logger;
use App\Infra\Migrator;
use App\Infra\Paths;
use App\Jobs\JobLauncher;
use App\Jobs\JobMaintenance;
use App\Jobs\JobRepository;
use App\Jobs\Worker;
use App\Tests\TempDir;
use PDO;
use PHPUnit\Framework\TestCase;

/** Runs the real Worker in-process against a fake job script: success, partial, crash, timeout, cancel, orphan. */
final class WorkerTest extends TestCase
{
    private string $dir;
    private Paths $paths;
    private PDO $pdo;
    private JobRepository $jobs;

    protected function setUp(): void
    {
        $this->dir = TempDir::create();
        $this->paths = new Paths($this->dir);
        $this->pdo = Database::open($this->paths);
        (new Migrator($this->pdo, dirname(__DIR__, 2) . '/migrations'))->migrate();
        $this->jobs = new JobRepository($this->pdo);
    }

    protected function tearDown(): void
    {
        unset($this->pdo, $this->jobs);
        TempDir::remove($this->dir);
    }

    /** @param array<string,mixed> $options */
    private function runWorker(array $options = []): void
    {
        putenv('APP_DATA_DIR=' . $this->dir);
        $config = Config::load();
        putenv('APP_DATA_DIR');
        $fake = dirname(__DIR__) . '/fixtures/fake_job.php';
        $worker = new Worker(
            $config,
            $this->paths,
            $this->jobs,
            JobLauncher::processControl(),
            new Logger($this->paths, 'worker.log'),
            $options + [
                'idleExitAfter' => 0.5,
                'commands' => static fn (string $type, string $spec) => ['command' => [PHP_BINARY, $fake, '--spec', $spec], 'cwd' => null],
            ],
        );
        $this->assertSame(0, $worker->run(false));
    }

    /** @return array<string,mixed> */
    private function enqueue(string $mode, string $type = 'selfcheck'): array
    {
        return $this->jobs->enqueue($type, null, ['mode' => $mode]);
    }

    public function testSuccessCopiesProgressStatsAndResult(): void
    {
        $id = $this->enqueue('success')['id'];
        $this->runWorker();
        $job = $this->jobs->find($id);
        $this->assertSame('succeeded', $job['status']);
        $this->assertSame(['current' => 3, 'total' => 3, 'label' => 'step 3'], $job['progress']);
        $this->assertSame(['pages' => 7, 'bytes' => 1234], $job['stats']);
        $this->assertSame(['path' => 'x.json', 'status' => 'ok'], $job['result']);
        $log = (string) file_get_contents($this->paths->data('jobs', (string) $id, 'job.log'));
        $this->assertStringContainsString('INFO hello from fake job', $log);
        $this->assertStringContainsString('this line is not JSON', $log);
        $this->assertFileDoesNotExist($this->paths->data('jobs', (string) $id, 'events.ndjson'));
    }

    public function testExitCodeOneIsPartialAndKeepsResult(): void
    {
        $id = $this->enqueue('partial')['id'];
        $this->runWorker();
        $job = $this->jobs->find($id);
        $this->assertSame('partial', $job['status']);
        $this->assertSame('partial', $job['result']['status']);
    }

    public function testCrashFailsAndKeepsStderrInTheLog(): void
    {
        $id = $this->enqueue('crash')['id'];
        $this->runWorker();
        $job = $this->jobs->find($id);
        $this->assertSame('failed', $job['status']);
        $this->assertStringContainsString('exited with code 3', (string) $job['error']);
        $this->assertStringContainsString('Traceback: boom', (string) file_get_contents($this->paths->data('jobs', (string) $id, 'job.log')));
    }

    public function testCancelRequestedWhileRunning(): void
    {
        $id = $this->enqueue('sleep')['id'];
        $this->pdo->exec("UPDATE jobs SET cancel_requested = 1 WHERE id = $id"); // as if cancel arrived right after the claim
        $started = microtime(true);
        $this->runWorker();
        $this->assertLessThan(15, microtime(true) - $started);
        $this->assertSame('canceled', $this->jobs->find($id)['status']);
    }

    public function testTimeoutStopsTheJobAndMarksItFailed(): void
    {
        $id = $this->enqueue('sleep')['id'];
        $this->runWorker(['timeouts' => ['selfcheck' => 2]]);
        $job = $this->jobs->find($id);
        $this->assertSame('failed', $job['status']);
        $this->assertSame('timeout', $job['error']);
    }

    public function testUnimplementedJobTypeFailsClearly(): void
    {
        $id = $this->enqueue('success', 'crawl')['id'];
        $this->runWorker(['commands' => static fn () => null]);
        $job = $this->jobs->find($id);
        $this->assertSame('failed', $job['status']);
        $this->assertStringContainsString('not implemented', (string) $job['error']);
    }

    public function testSecondWorkerExitsWhileTheLockIsHeld(): void
    {
        $this->paths->ensureDir('run');
        $lock = fopen($this->paths->data('run', 'worker.lock'), 'c');
        $this->assertTrue(flock($lock, LOCK_EX | LOCK_NB));
        $id = $this->enqueue('success')['id'];
        $this->runWorker(); // returns at once without touching the queue
        $this->assertSame('queued', $this->jobs->find($id)['status']);
        flock($lock, LOCK_UN);
        fclose($lock);
    }

    public function testOrphanedRunningJobIsFailedWhenNoWorkerIsAlive(): void
    {
        $id = $this->enqueue('sleep')['id'];
        $this->pdo->exec("UPDATE jobs SET status='running', worker_pid=999999, heartbeat_at=strftime('%Y-%m-%dT%H:%M:%fZ','now','-5 minutes') WHERE id=$id");
        putenv('APP_DATA_DIR=' . $this->dir);
        $launcher = new JobLauncher(Config::load(), $this->paths, $this->jobs);
        putenv('APP_DATA_DIR');
        $this->assertFalse($launcher->ensureWorker(), 'nothing queued, so no worker is spawned');
        $job = $this->jobs->find($id);
        $this->assertSame('failed', $job['status']);
        $this->assertSame('Worker stopped unexpectedly', $job['error']);
    }

    public function testFreshHeartbeatIsNotAnOrphan(): void
    {
        $id = $this->enqueue('sleep')['id'];
        $this->pdo->exec("UPDATE jobs SET status='running', heartbeat_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=$id");
        $this->assertSame(0, $this->jobs->failOrphans());
        $this->assertSame('running', $this->jobs->find($id)['status']);
    }

    public function testPruningRemovesOnlyOldFinishedJobFolders(): void
    {
        $old = $this->enqueue('success')['id'];
        $recent = $this->enqueue('success')['id'];
        $active = $this->enqueue('sleep')['id'];
        $this->pdo->exec("UPDATE jobs SET status='succeeded', finished_at=strftime('%Y-%m-%dT%H:%M:%fZ','now','-40 days') WHERE id=$old");
        $this->pdo->exec("UPDATE jobs SET status='succeeded', finished_at=strftime('%Y-%m-%dT%H:%M:%fZ','now','-2 days') WHERE id=$recent");
        foreach ([$old, $recent, $active] as $id) {
            $this->paths->ensureDir('jobs', (string) $id);
            file_put_contents($this->paths->data('jobs', (string) $id, 'job.log'), 'x');
        }
        $this->assertSame(1, (new JobMaintenance($this->jobs, $this->paths))->pruneFinished());
        $this->assertDirectoryDoesNotExist($this->paths->data('jobs', (string) $old));
        $this->assertDirectoryExists($this->paths->data('jobs', (string) $recent));
        $this->assertDirectoryExists($this->paths->data('jobs', (string) $active));
        $this->assertNull($this->jobs->row($old)['log_path']);
    }
}
