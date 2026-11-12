<?php
declare(strict_types=1);

namespace App\Jobs;

use App\Infra\Config;
use App\Infra\Paths;
use App\Infra\Platform;
use App\Jobs\Process\PosixProcess;
use App\Jobs\Process\ProcessControl;
use App\Jobs\Process\WindowsProcess;

final class JobLauncher
{
    public function __construct(
        private readonly Config $config,
        private readonly Paths $paths,
        private readonly JobRepository $jobs,
    ) {
    }

    public static function processControl(): ProcessControl
    {
        return Platform::isWindows() ? new WindowsProcess() : new PosixProcess();
    }

    public function lockPath(): string
    {
        return $this->paths->data('run', 'worker.lock');
    }

    /** True when no worker (spawned or daemon) holds the lock. */
    public function workerIsIdle(): bool
    {
        $this->paths->ensureDir('run');
        $fh = fopen($this->lockPath(), 'c');
        if ($fh === false) {
            return false;
        }
        $free = flock($fh, LOCK_EX | LOCK_NB);
        if ($free) {
            flock($fh, LOCK_UN);
        }
        fclose($fh);
        return $free;
    }

    /** Idempotent: spawns a worker only when work is queued and none is alive. Also recovers orphans. */
    public function ensureWorker(): bool
    {
        if (!$this->workerIsIdle()) {
            return false;
        }
        $this->jobs->failOrphans();
        if ($this->jobs->queuedCount() === 0) {
            return false;
        }
        $script = $this->config->string('backend_dir') . DIRECTORY_SEPARATOR . 'bin' . DIRECTORY_SEPARATOR . 'worker.php';
        self::processControl()->spawnDetached($this->config->string('php_cli'), $script);
        return true;
    }
}
