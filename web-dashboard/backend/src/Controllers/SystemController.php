<?php
declare(strict_types=1);

namespace App\Controllers;

use App\App;
use App\Http\HttpException;
use App\Http\Request;
use App\Http\Response;
use App\Infra\Backup;
use App\Infra\Platform;

final class SystemController
{
    private const EXTENSIONS = ['pdo_sqlite', 'intl', 'zip', 'mbstring', 'fileinfo', 'openssl'];

    public function __construct(private readonly App $app)
    {
    }

    public function health(Request $request): Response
    {
        $config = $this->app->config;
        $pdo = $this->app->pdo();
        $ext = [];
        foreach (self::EXTENSIONS as $name) {
            $ext[$name] = extension_loaded($name);
        }
        $sqlite = (string) $pdo->query('SELECT sqlite_version()')->fetchColumn();
        $fts5 = false;
        try {
            $pdo->exec('CREATE VIRTUAL TABLE temp.fts5_probe USING fts5(x)');
            $pdo->exec('DROP TABLE temp.fts5_probe');
            $fts5 = true;
        } catch (\Throwable) {
        }
        $dataDir = $config->dataDir();
        $migrations = $this->app->migrator()->status();
        $launcher = $this->app->launcher();

        return Response::json([
            'ok' => !in_array(false, $ext, true) && is_writable($dataDir) && $migrations['pending'] === [],
            'version' => $this->app->version(),
            'env' => [
                'os' => PHP_OS_FAMILY,
                'php' => PHP_VERSION,
                'phpSapi' => PHP_SAPI,
                'extensions' => $ext,
                'sqlite' => $sqlite,
                'fts5' => $fts5,
                'dataDir' => $dataDir,
                'dataDirWritable' => is_writable($dataDir),
                'python' => $this->python($config->string('python')),
                'phpCli' => $this->phpCli($config->string('php_cli')),
            ],
            'worker' => ['idle' => $launcher->workerIsIdle(), 'queued' => $this->app->jobs()->queuedCount()],
            'migrations' => ['current' => $migrations['current'], 'latest' => $migrations['latest']],
            'legacyData' => $this->legacyData(),
        ]);
    }

    public function createBackup(Request $request): Response
    {
        $info = (new Backup($this->app->pdo(), $this->app->paths))->create('manual');
        return Response::json($info, 201);
    }

    public function listBackups(Request $request): Response
    {
        $dir = $this->app->paths->data('backups');
        $items = [];
        foreach (glob($dir . DIRECTORY_SEPARATOR . 'app-*.sqlite') ?: [] as $file) {
            $items[] = ['file' => basename($file), 'bytes' => (int) filesize($file), 'createdAt' => gmdate('c', (int) filemtime($file))];
        }
        usort($items, static fn ($a, $b) => strcmp($b['file'], $a['file']));
        return Response::json(['items' => $items]);
    }

    /** @param array<string,string> $p */
    public function downloadBackup(Request $request, array $p): Response
    {
        // Only files this app wrote, matched by name: no path can be smuggled in.
        $name = $p['name'];
        if (!preg_match('/^app-[0-9T-]+-[a-z0-9-]+\.sqlite$/i', $name)) {
            throw new HttpException(404, 'not_found', 'Backup not found.');
        }
        $file = $this->app->paths->data('backups', $name);
        if (!is_file($file)) {
            throw new HttpException(404, 'not_found', 'Backup not found.');
        }
        return Response::file($file, $name, 'application/vnd.sqlite3');
    }

    /** M0 spawn spike: a no-op job that outlives a php-cgi recycle and can be canceled. */
    public function spawnTest(Request $request): Response
    {
        $body = $request->json();
        $seconds = max(5, min(600, (int) ($body['seconds'] ?? 180)));
        $job = $this->app->jobs()->enqueue('selfcheck', null, ['seconds' => $seconds]);
        $this->app->launcher()->ensureWorker();
        return Response::json(['job' => $job], 202);
    }

    /** @return array{path:string,version:?string,ok:bool} */
    private function python(string $path): array
    {
        $out = $this->capture([$path, '-c', 'import sys; print(sys.version.split()[0])']);
        $version = $out !== null ? trim($out) : null;
        return ['path' => $path, 'version' => $version ?: null, 'ok' => $version !== null && version_compare($version, '3.11', '>=')];
    }

    /** @return array{path:string,ok:bool} */
    private function phpCli(string $path): array
    {
        $out = $this->capture([$path, '-r', 'echo PHP_SAPI;']);
        return ['path' => $path, 'ok' => $out === 'cli'];
    }

    /** @param list<string> $command */
    private function capture(array $command): ?string
    {
        try {
            $proc = @proc_open($command, [['file', Platform::nullDevice(), 'r'], ['pipe', 'w'], ['file', Platform::nullDevice(), 'w']], $pipes);
        } catch (\Throwable) {
            return null;
        }
        if (!is_resource($proc)) {
            return null;
        }
        $out = (string) stream_get_contents($pipes[1]);
        fclose($pipes[1]);
        return proc_close($proc) === 0 ? $out : null;
    }

    /** @return list<string> */
    private function legacyData(): array
    {
        $root = $this->app->config->string('project_root');
        $found = [];
        foreach (['backend/data/app.sqlite', 'dist/backend/data/app.sqlite'] as $rel) {
            $file = $root . '/' . $rel;
            if (is_file($file)) {
                $found[] = $file;
            }
        }
        return $found;
    }
}
