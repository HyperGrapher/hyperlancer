<?php
declare(strict_types=1);

namespace App\Jobs\Process;

final class WindowsProcess implements ProcessControl
{
    public function spawnDetached(string $phpCli, string $script): void
    {
        // Paths come from validated config only (see Config::validate); no user data reaches this line.
        $handle = popen('start "" /B "' . $phpCli . '" "' . $script . '"', 'r');
        if ($handle !== false) {
            pclose($handle);
        }
    }

    public function commandPrefix(): array
    {
        return [];
    }

    public function terminateTree(int $pid, bool $force): void
    {
        // Soft stop on Windows is the cancel file; only the hard kill goes through taskkill.
        if (!$force) {
            return;
        }
        $proc = proc_open(['taskkill', '/PID', (string) $pid, '/T', '/F'], [['pipe', 'r'], ['pipe', 'w'], ['pipe', 'w']], $pipes);
        if (is_resource($proc)) {
            foreach ($pipes as $pipe) {
                fclose($pipe);
            }
            proc_close($proc);
        }
    }

    public function isAlive(int $pid): bool
    {
        $proc = proc_open(['tasklist', '/FI', 'PID eq ' . $pid, '/NH'], [['pipe', 'r'], ['pipe', 'w'], ['pipe', 'w']], $pipes);
        if (!is_resource($proc)) {
            return false;
        }
        $out = (string) stream_get_contents($pipes[1]);
        foreach ($pipes as $pipe) {
            fclose($pipe);
        }
        proc_close($proc);
        return str_contains($out, (string) $pid);
    }
}
