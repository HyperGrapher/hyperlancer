<?php
declare(strict_types=1);

namespace App\Jobs\Process;

final class PosixProcess implements ProcessControl
{
    private function setsid(): ?string
    {
        foreach (['/usr/bin/setsid', '/bin/setsid', '/usr/local/bin/setsid'] as $path) {
            if (is_executable($path)) {
                return $path;
            }
        }
        return null;
    }

    public function spawnDetached(string $phpCli, string $script): void
    {
        $setsid = $this->setsid();
        $cmd = ($setsid ? escapeshellarg($setsid) . ' ' : '') . 'nohup ' . escapeshellarg($phpCli) . ' ' . escapeshellarg($script)
            . ' >/dev/null 2>&1 &';
        exec($cmd);
    }

    public function commandPrefix(): array
    {
        $setsid = $this->setsid();
        return $setsid ? [$setsid] : [];
    }

    public function terminateTree(int $pid, bool $force): void
    {
        $signal = $force ? 9 : 15;
        if (!function_exists('posix_kill')) {
            exec(sprintf('kill -%d -- -%d %d 2>/dev/null', $signal, $pid, $pid));
            return;
        }
        // The child leads its own group when started through setsid; fall back to the pid alone.
        if (!@posix_kill(-$pid, $signal)) {
            @posix_kill($pid, $signal);
        }
    }

    public function isAlive(int $pid): bool
    {
        if (function_exists('posix_kill')) {
            return @posix_kill($pid, 0);
        }
        return is_dir("/proc/$pid");
    }
}
