<?php
declare(strict_types=1);

namespace App\Infra;

use RuntimeException;

/**
 * Resolution order per key: environment variable, <project_root>/config.local.php, OS default
 * (docs/PRD.md §10.3).
 */
final class Config
{
    /** @param array<string,mixed> $values */
    private function __construct(private array $values)
    {
    }

    public static function load(?string $backendDir = null): self
    {
        $backendDir ??= dirname(__DIR__, 2);
        $root = self::projectRoot($backendDir);

        $local = [];
        $localFile = $root . DIRECTORY_SEPARATOR . 'config.local.php';
        if (is_file($localFile)) {
            $loaded = require $localFile;
            if (is_array($loaded)) {
                $local = $loaded;
            }
        }

        $win = Platform::isWindows();
        $sep = DIRECTORY_SEPARATOR;
        $venvPython = $win
            ? $root . '\\scripts\\.venv\\Scripts\\python.exe'
            : $root . '/scripts/.venv/bin/python';

        $defaults = [
            'env' => 'production',
            'project_root' => $root,
            'backend_dir' => $backendDir,
            'data_dir' => $root . $sep . 'data',
            'python' => is_file($venvPython) ? $venvPython : ($win ? 'python' : 'python3'),
            'php_cli' => self::defaultPhpCli(),
            'allowed_hosts' => $win
                ? ['dashboard.test', 'localhost', '127.0.0.1']
                : ['localhost', '127.0.0.1', '[::1]'],
        ];

        $env = [
            'env' => getenv('APP_ENV') ?: null,
            'data_dir' => getenv('APP_DATA_DIR') ?: null,
            'python' => getenv('APP_PYTHON') ?: null,
            'php_cli' => getenv('APP_PHP_CLI') ?: null,
            'allowed_hosts' => getenv('APP_ALLOWED_HOSTS')
                ? array_values(array_filter(array_map('trim', explode(',', (string) getenv('APP_ALLOWED_HOSTS')))))
                : null,
        ];

        $values = $defaults;
        foreach ($local as $key => $value) {
            if ($key !== 'project_root' && $key !== 'backend_dir') {
                $values[$key] = $value;
            }
        }
        foreach ($env as $key => $value) {
            if ($value !== null) {
                $values[$key] = $value;
            }
        }
        $config = new self($values);
        $config->validate();
        return $config;
    }

    /**
     * Built copies find the repo through build-info.php (written by the Vite build), so a junction-resolved
     * __DIR__ never decides where data lives (D10).
     */
    private static function projectRoot(string $backendDir): string
    {
        $info = $backendDir . DIRECTORY_SEPARATOR . 'build-info.php';
        if (is_file($info)) {
            $loaded = require $info;
            if (is_array($loaded) && is_string($loaded['project_root'] ?? null)) {
                return $loaded['project_root'];
            }
        }
        return dirname($backendDir);
    }

    private static function defaultPhpCli(): string
    {
        if (Platform::isWindows()) {
            return dirname(PHP_BINARY) . '\\php.exe';
        }
        return Platform::isCli() && PHP_BINARY !== '' ? PHP_BINARY : 'php';
    }

    private function validate(): void
    {
        // The data dir must never live in a web-served or build-wiped location (D10).
        $data = rtrim(str_replace('\\', '/', $this->string('data_dir')), '/') . '/';
        $guarded = [rtrim(str_replace('\\', '/', $this->string('backend_dir')), '/') . '/'];
        if (is_file($this->string('backend_dir') . DIRECTORY_SEPARATOR . 'build-info.php')) {
            $guarded[] = rtrim(str_replace('\\', '/', dirname($this->string('backend_dir'))), '/') . '/';
        }
        foreach ($guarded as $dir) {
            if (str_starts_with(strtolower($data), strtolower($dir))) {
                throw new RuntimeException(
                    'The data directory (' . $this->string('data_dir') . ') is inside the web-served build output. '
                    . 'Set APP_DATA_DIR or data_dir in config.local.php to a folder outside dist/.'
                );
            }
        }
        if (Platform::isWindows()) {
            // These paths reach `cmd /c start`, so they must be inert (docs/PRD.md §9.3).
            foreach (['php_cli', 'backend_dir'] as $key) {
                if (preg_match('/["%^&|<>]/', (string) $this->values[$key])) {
                    throw new RuntimeException("Config '$key' contains characters that are unsafe in a command line.");
                }
            }
        }
    }

    public function get(string $key): mixed
    {
        return $this->values[$key] ?? null;
    }

    public function string(string $key): string
    {
        return (string) $this->values[$key];
    }

    public function isDev(): bool
    {
        return $this->values['env'] === 'development';
    }

    /** @return list<string> */
    public function allowedHosts(): array
    {
        return array_map('strtolower', (array) $this->values['allowed_hosts']);
    }

    public function dataDir(): string
    {
        return $this->string('data_dir');
    }
}
