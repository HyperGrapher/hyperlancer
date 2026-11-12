<?php
declare(strict_types=1);

namespace App\Jobs;

use App\Infra\Config;
use App\Infra\Logger;
use App\Infra\Paths;
use App\Infra\Platform;
use App\Jobs\Process\ProcessControl;

/** Claims queued jobs, runs each as a child process, and mirrors its NDJSON events into the jobs table. */
final class Worker
{
    private const HEARTBEAT_EVERY = 2.0;
    private const CANCEL_POLL_EVERY = 1.0;
    private const LOG_CAP_BYTES = 10_000_000;

    /** Seconds before a job is killed as timed out, per type (docs/PRD.md §9.2). */
    private const TIMEOUTS = ['profile' => 120, 'crawl' => 7200, 'archive' => 1800, 'backup' => 300, 'selfcheck' => 600];

    /** @var array<int,array<string,mixed>> */
    private array $running = [];

    private readonly float $idleExitAfter;
    private readonly float $hardKillAfter;
    /** @var array<string,int> */
    private readonly array $timeouts;
    /** @var (callable(string,string):?array{command:list<string>,cwd:?string})|null */
    private $commandResolver;
    /** @var (callable(array<string,mixed>,string):void)|null runs after a job row reached its final status */
    private $onFinish;

    /**
     * @param array{idleExitAfter?:float,hardKillAfter?:float,timeouts?:array<string,int>,commands?:callable,onFinish?:callable} $options
     *        Overrides for tests: shorter idle exit and timeouts, and a command resolver for fake job scripts.
     */
    public function __construct(
        private readonly Config $config,
        private readonly Paths $paths,
        private readonly JobRepository $jobs,
        private readonly ProcessControl $process,
        private readonly Logger $log,
        array $options = [],
    ) {
        $this->idleExitAfter = (float) ($options['idleExitAfter'] ?? 10.0);
        $this->hardKillAfter = (float) ($options['hardKillAfter'] ?? 5.0);
        $this->timeouts = array_merge(self::TIMEOUTS, $options['timeouts'] ?? []);
        $this->commandResolver = $options['commands'] ?? null;
        $this->onFinish = $options['onFinish'] ?? null;
    }

    public function run(bool $daemon): int
    {
        $this->paths->ensureDir('run');
        $lock = fopen($this->paths->data('run', 'worker.lock'), 'c');
        if ($lock === false || !flock($lock, LOCK_EX | LOCK_NB)) {
            return 0; // another worker is alive
        }
        $this->log->info('worker started pid=' . getmypid() . ($daemon ? ' (daemon)' : ''), 'worker');
        // A fresh worker means any job still marked running lost its owner.
        $this->jobs->failOrphans(0);
        try {
            $pruned = (new JobMaintenance($this->jobs, $this->paths))->pruneFinished();
            if ($pruned > 0) {
                $this->log->info("pruned $pruned old job folder(s)", 'worker');
            }
        } catch (\Throwable $e) {
            $this->log->error('pruning failed: ' . $e->getMessage(), 'worker');
        }

        $idleSince = microtime(true);
        while (true) {
            foreach (array_keys(JobRepository::LIMITS) as $type) {
                while ($this->countRunning($type) < JobRepository::LIMITS[$type]) {
                    $row = $this->jobs->claim($type, (int) getmypid());
                    if ($row === null) {
                        break;
                    }
                    $this->start($row);
                }
            }
            foreach (array_keys($this->running) as $id) {
                $this->tick($id);
            }
            if ($this->running || $this->jobs->queuedCount() > 0) {
                $idleSince = microtime(true);
            } elseif (!$daemon && microtime(true) - $idleSince > $this->idleExitAfter) {
                break;
            }
            usleep($this->running ? 250_000 : 1_000_000);
        }
        $this->log->info('worker exiting (idle)', 'worker');
        flock($lock, LOCK_UN);
        fclose($lock);
        return 0;
    }

    private function countRunning(string $type): int
    {
        return count(array_filter($this->running, static fn ($j) => $j['type'] === $type));
    }

    /** @param array<string,mixed> $row */
    private function start(array $row): void
    {
        $id = (int) $row['id'];
        $type = (string) $row['type'];
        $dir = $this->paths->ensureDir('jobs', (string) $id);
        $cancelFile = $dir . DIRECTORY_SEPARATOR . 'cancel';
        @unlink($cancelFile);
        $eventsFile = $dir . DIRECTORY_SEPARATOR . 'events.ndjson';
        $logFile = $dir . DIRECTORY_SEPARATOR . 'job.log';
        file_put_contents($eventsFile, '');

        $params = json_decode((string) $row['params_json'], true) ?: [];
        $spec = $dir . DIRECTORY_SEPARATOR . 'spec.json';
        file_put_contents($spec, json_encode(['jobId' => $id, 'type' => $type, 'params' => $params, 'cancelFile' => $cancelFile, 'dir' => $dir], JSON_UNESCAPED_SLASHES));

        $resolved = $this->command($type, $spec);
        if ($resolved === null) {
            $this->jobs->finish($id, 'failed', null, "Job type '$type' is not implemented yet.");
            $this->log->error("unsupported job type $type", "jobs#$id");
            return;
        }

        // Output goes to files rather than pipes: Windows pipes cannot be read without blocking.
        $proc = proc_open(
            array_merge($this->process->commandPrefix(), $resolved['command']),
            [0 => ['file', Platform::nullDevice(), 'r'], 1 => ['file', $eventsFile, 'w'], 2 => ['file', $logFile, 'a']],
            $pipes,
            $resolved['cwd'],
            null,
            ['bypass_shell' => true],
        );
        if (!is_resource($proc)) {
            $this->jobs->finish($id, 'failed', null, 'Could not start the job process.');
            $this->log->error('proc_open failed', "jobs#$id");
            return;
        }
        $status = proc_get_status($proc);
        $this->jobs->setChildPid($id, (int) $status['pid']);
        $this->log->info("started $type pid={$status['pid']}", "jobs#$id");

        $this->running[$id] = [
            'type' => $type, 'proc' => $proc, 'pid' => (int) $status['pid'], 'events' => $eventsFile, 'offset' => 0, 'buffer' => '',
            'log' => $logFile, 'cancelFile' => $cancelFile, 'startedAt' => microtime(true),
            'lastHeartbeat' => 0.0, 'lastCancelPoll' => 0.0, 'cancelSentAt' => null, 'result' => null, 'timedOut' => false,
        ];
    }

    /** @return array{command:list<string>,cwd:?string}|null */
    private function command(string $type, string $spec): ?array
    {
        if ($this->commandResolver !== null) {
            return ($this->commandResolver)($type, $spec);
        }
        $bin = $this->config->string('backend_dir') . DIRECTORY_SEPARATOR . 'bin' . DIRECTORY_SEPARATOR;
        return match ($type) {
            'selfcheck' => ['command' => [$this->config->string('php_cli'), $bin . 'selfcheck.php', '--spec', $spec], 'cwd' => null],
            // Python tools run from the scripts folder so `-m site_profile` finds its package.
            'profile' => ['command' => [$this->config->string('python'), '-m', 'site_profile', '--spec', $spec], 'cwd' => $this->scriptsDir()],
            'crawl' => ['command' => [$this->config->string('python'), '-m', 'site_crawl', '--spec', $spec], 'cwd' => $this->scriptsDir()],
            'archive' => ['command' => [$this->config->string('php_cli'), $bin . 'archive.php', '--spec', $spec], 'cwd' => null],
            default => null,
        };
    }

    public function scriptsDir(): string
    {
        return $this->config->string('project_root') . DIRECTORY_SEPARATOR . 'scripts';
    }

    private function tick(int $id): void
    {
        $job = &$this->running[$id];
        $now = microtime(true);
        $this->readEvents($id, $job);

        if ($now - $job['lastHeartbeat'] >= self::HEARTBEAT_EVERY) {
            $this->jobs->heartbeat($id);
            $job['lastHeartbeat'] = $now;
        }
        if ($job['cancelSentAt'] === null && $now - $job['lastCancelPoll'] >= self::CANCEL_POLL_EVERY) {
            $job['lastCancelPoll'] = $now;
            if ($this->jobs->cancelRequested($id)) {
                $this->signalCancel($id, $job);
            }
        }
        if ($job['cancelSentAt'] === null && $now - $job['startedAt'] > ($this->timeouts[$job['type']] ?? 600)) {
            $job['timedOut'] = true;
            $this->signalCancel($id, $job);
        }
        if ($job['cancelSentAt'] !== null && $now - $job['cancelSentAt'] > $this->hardKillAfter && !($job['hardKilled'] ?? false)) {
            $this->process->terminateTree($job['pid'], true);
            $job['hardKilled'] = true;
        }

        $status = proc_get_status($job['proc']);
        if ($status['running']) {
            return;
        }
        $this->readEvents($id, $job);
        $exit = $status['exitcode'];
        proc_close($job['proc']);
        $this->conclude($id, $job, (int) $exit);
        unset($job, $this->running[$id]);
    }

    /** @param array<string,mixed> $job */
    private function signalCancel(int $id, array &$job): void
    {
        touch($job['cancelFile']);
        $this->process->terminateTree($job['pid'], false);
        $job['cancelSentAt'] = microtime(true);
        $this->log->info($job['timedOut'] ? 'timeout, stopping' : 'cancel requested, stopping', "jobs#$id");
    }

    /** @param array<string,mixed> $job */
    private function conclude(int $id, array $job, int $exit): void
    {
        $canceled = $this->jobs->cancelRequested($id);
        if ($job['timedOut']) {
            $this->jobs->finish($id, 'failed', $exit, 'timeout', $job['result']);
        } elseif ($canceled) {
            $this->jobs->finish($id, 'canceled', $exit, null, $job['result']);
        } elseif ($exit === 0) {
            $this->jobs->finish($id, 'succeeded', 0, null, $job['result']);
        } elseif ($exit === 1) {
            $this->jobs->finish($id, 'partial', 1, null, $job['result']);
        } else {
            $this->jobs->finish($id, 'failed', $exit, "Process exited with code $exit", $job['result']);
        }
        @unlink($job['events']); // the interesting parts live in the jobs row and job.log
        if ($this->onFinish !== null) {
            try {
                $final = $this->jobs->row($id);
                if ($final !== null) {
                    ($this->onFinish)($final, (string) $final['status']);
                }
            } catch (\Throwable $e) {
                $this->log->error('finish hook failed: ' . $e->getMessage(), "jobs#$id");
            }
        }
        $this->log->info("finished exit=$exit", "jobs#$id");
    }

    /** @param array<string,mixed> $job */
    private function readEvents(int $id, array &$job): void
    {
        clearstatcache(true, $job['events']);
        $size = (int) @filesize($job['events']);
        if ($size <= $job['offset']) {
            return;
        }
        $fh = fopen($job['events'], 'rb');
        if ($fh === false) {
            return;
        }
        fseek($fh, $job['offset']);
        $chunk = (string) fread($fh, $size - $job['offset']);
        fclose($fh);
        $job['offset'] += strlen($chunk);
        $job['buffer'] .= $chunk;
        while (($pos = strpos($job['buffer'], "\n")) !== false) {
            $line = trim(substr($job['buffer'], 0, $pos));
            $job['buffer'] = substr($job['buffer'], $pos + 1);
            if ($line !== '') {
                $this->apply($id, $job, $line);
            }
        }
    }

    /** Lines written by the worker stop at 10 MB so a chatty job cannot fill the disk. */
    private function appendLog(array &$job, string $line): void
    {
        clearstatcache(true, $job['log']);
        $size = (int) @filesize($job['log']);
        if ($size > self::LOG_CAP_BYTES) {
            if (empty($job['logCapped'])) {
                file_put_contents($job['log'], '[log truncated: size limit reached]' . PHP_EOL, FILE_APPEND);
                $job['logCapped'] = true;
            }
            return;
        }
        file_put_contents($job['log'], $line . PHP_EOL, FILE_APPEND);
    }

    /** @param array<string,mixed> $job */
    private function apply(int $id, array &$job, string $line): void
    {
        $event = json_decode($line, true);
        if (!is_array($event)) {
            $this->appendLog($job, $line);
            return;
        }
        switch ($event['type'] ?? '') {
            case 'progress':
                $this->jobs->progress($id, isset($event['current']) ? (int) $event['current'] : null, isset($event['total']) ? (int) $event['total'] : null, isset($event['label']) ? (string) $event['label'] : null);
                break;
            case 'stats':
                unset($event['type']);
                $this->jobs->stats($id, $event);
                break;
            case 'log':
                $this->appendLog($job, gmdate('H:i:s') . ' ' . strtoupper((string) ($event['level'] ?? 'info')) . ' ' . ($event['message'] ?? ''));
                break;
            case 'result':
                unset($event['type']);
                $job['result'] = $event;
                break;
        }
    }
}
