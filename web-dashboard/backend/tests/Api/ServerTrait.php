<?php
declare(strict_types=1);

namespace App\Tests\Api;

use App\Tests\TempDir;

/** Boots `php -S` on a temp data dir for the using test class (statics are per using class). */
trait ServerTrait
{
    private static string $data;
    /** @var resource */
    private static $server;
    private static string $base;

    public static function setUpBeforeClass(): void
    {
        self::startServer();
    }

    private static function startServer(): void
    {
        self::$data = TempDir::create();
        $port = random_int(20000, 40000);
        self::$base = "http://127.0.0.1:$port";
        $env = array_merge(getenv(), ['APP_DATA_DIR' => self::$data, 'APP_ENV' => 'development']); // Windows needs SystemRoot etc.
        $null = PHP_OS_FAMILY === 'Windows' ? 'NUL' : '/dev/null';
        self::$server = proc_open(
            [PHP_BINARY, '-S', "127.0.0.1:$port", dirname(__DIR__, 2) . '/dev-router.php'],
            [['file', $null, 'r'], ['file', $null, 'w'], ['file', $null, 'w']],
            $pipes,
            null,
            $env,
        );
        for ($i = 0; $i < 50; $i++) {
            if (@fsockopen('127.0.0.1', $port)) {
                return;
            }
            usleep(100_000);
        }
        self::fail('dev server did not start');
    }

    public static function tearDownAfterClass(): void
    {
        self::stopServer();
    }

    private static function stopServer(): void
    {
        proc_terminate(self::$server);
        proc_close(self::$server);
        usleep(300_000); // let a lingering idle worker finish before deleting its files
        TempDir::remove(self::$data);
    }

    /** @param array<string,mixed>|null $body @param list<string> $headers @return array{int,array<string,mixed>} */
    private function call(string $method, string $path, ?array $body = null, array $headers = []): array
    {
        $defaults = array_merge(['Host: localhost'], $method === 'GET' ? [] : ['X-Requested-With: freelance-dashboard', 'Content-Type: application/json']);
        $overridden = array_map(static fn ($h) => strtolower(strstr($h, ':', true)), $headers);
        $headers = array_merge(array_filter($defaults, static fn ($h) => !in_array(strtolower(strstr($h, ':', true)), $overridden, true)), $headers);
        $ctx = stream_context_create(['http' => [
            'method' => $method,
            'header' => implode("\r\n", $headers),
            'content' => $body === null ? ($method === 'GET' ? '' : '{}') : json_encode($body),
            'ignore_errors' => true,
        ]]);
        $text = (string) file_get_contents(self::$base . $path, false, $ctx);
        preg_match('#HTTP/\S+ (\d+)#', $http_response_header[0] ?? '', $m);
        return [(int) ($m[1] ?? 0), json_decode($text, true) ?? []];
    }
}
