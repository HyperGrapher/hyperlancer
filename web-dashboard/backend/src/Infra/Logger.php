<?php
declare(strict_types=1);

namespace App\Infra;

/** Human-readable lines: `2026-10-26 14:02:11 ERROR [jobs#42] message`. */
final class Logger
{
    public function __construct(private readonly Paths $paths, private readonly string $file = 'app.log')
    {
    }

    public function log(string $level, string $message, string $context = ''): void
    {
        try {
            $dir = $this->paths->ensureDir('logs');
            $name = preg_replace('/\.log$/', '', $this->file) . '-' . gmdate('Y-m-d') . '.log';
            $line = gmdate('Y-m-d H:i:s') . ' ' . strtoupper($level) . ($context !== '' ? " [$context]" : '')
                . ' ' . str_replace(["\r", "\n"], ' ', $message) . PHP_EOL;
            file_put_contents($dir . DIRECTORY_SEPARATOR . $name, $line, FILE_APPEND | LOCK_EX);
            $this->prune($dir);
        } catch (\Throwable) {
            // Logging must never break a request.
        }
    }

    public function info(string $message, string $context = ''): void
    {
        $this->log('info', $message, $context);
    }

    public function error(string $message, string $context = ''): void
    {
        $this->log('error', $message, $context);
    }

    private function prune(string $dir): void
    {
        static $done = false;
        if ($done) {
            return;
        }
        $done = true;
        foreach (glob($dir . DIRECTORY_SEPARATOR . '*.log') ?: [] as $file) {
            if (filemtime($file) < time() - 14 * 86400) {
                @unlink($file);
            }
        }
    }
}
