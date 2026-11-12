<?php
declare(strict_types=1);

namespace App\Tests\Api;

use PHPUnit\Framework\TestCase;

/** A real crawl of a tiny local site through the API, worker and Python crawler. */
final class CrawlsApiTest extends TestCase
{
    use ServerTrait;

    /** @var resource */
    private static $site;
    private static string $siteUrl;

    public static function setUpBeforeClass(): void
    {
        $port = random_int(41000, 45000);
        self::$siteUrl = "http://127.0.0.1:$port/";
        $null = PHP_OS_FAMILY === 'Windows' ? 'NUL' : '/dev/null';
        self::$site = proc_open([PHP_BINARY, '-S', "127.0.0.1:$port", '-t', dirname(__DIR__) . '/fixtures/site'], [['file', $null, 'r'], ['file', $null, 'w'], ['file', $null, 'w']], $pipes);
        usleep(500_000);
        self::startServer();
    }

    public static function tearDownAfterClass(): void
    {
        proc_terminate(self::$site);
        proc_close(self::$site);
        self::stopServer();
    }

    public function testCrawlLifecycle(): void
    {
        [, $created] = $this->call('POST', '/api/sites', ['url' => self::$siteUrl, 'fetch' => false, 'allowPrivateNet' => true]);
        $siteId = $created['site']['id'];

        [$status, $bad] = $this->call('POST', "/api/sites/$siteId/crawls", ['maxPages' => 0, 'exclude' => ['(']]);
        $this->assertSame(422, $status);
        $this->assertArrayHasKey('maxPages', $bad['error']['fields']);
        $this->assertArrayHasKey('exclude.0', $bad['error']['fields']);
        [$status] = $this->call('POST', "/api/sites/$siteId/crawls", ['respectRobots' => false]);
        $this->assertSame(422, $status, 'ignoring robots.txt needs the confirmation');

        [$status, $body] = $this->call('POST', "/api/sites/$siteId/crawls", ['maxPages' => 10, 'delay' => 0]);
        $this->assertSame(202, $status, json_encode($body));
        $id = $body['crawl']['id'];
        [$status] = $this->call('POST', "/api/sites/$siteId/crawls", ['maxPages' => 10]);
        $this->assertSame(429, $status, 'one crawl at a time');

        $crawl = $this->waitFor($id, ['done', 'partial', 'failed', 'canceled'], 60);
        $this->assertSame('done', $crawl['status'], json_encode($crawl));
        $this->assertSame(2, $crawl['pages']);
        $this->assertSame(1, $crawl['images']);
        $this->assertSame('done', $crawl['manifest']['status']);

        [, $pages] = $this->call('GET', "/api/crawls/$id/pages");
        $this->assertCount(2, $pages['items']);
        [, $found] = $this->call('GET', "/api/crawls/$id/pages?q=about");
        $this->assertCount(1, $found['items']);
        [, $page] = $this->call('GET', "/api/crawls/$id/pages/" . $pages['items'][0]['id']);
        $this->assertStringContainsString('Welcome', $page['markdown']);
        $this->assertStringNotContainsString('Footer text', $page['markdown']);
        $this->assertStringNotContainsString('url:', $page['markdown'], 'front matter is stripped');

        [, $assets] = $this->call('GET', "/api/crawls/$id/assets?kind=image");
        $this->assertSame(1, $assets['total']);
        $file = $assets['items'][0]['file'];
        $ctx = stream_context_create(['http' => ['header' => 'Host: localhost', 'ignore_errors' => true]]);
        $png = (string) file_get_contents(self::$base . "/api/crawls/$id/raw?path=" . rawurlencode($file), false, $ctx);
        $this->assertStringStartsWith("\x89PNG", $png);
        $this->assertStringContainsString('nosniff', strtolower(implode("\n", $http_response_header)));

        foreach (['../../app.sqlite', '/etc/passwd', 'pages/../../x', 'manifest.json%00.png'] as $evil) {
            [$status] = $this->call('GET', "/api/crawls/$id/raw?path=" . rawurlencode($evil));
            $this->assertSame(404, $status, $evil);
        }

        [$status, $arch] = $this->call('POST', "/api/crawls/$id/archive");
        $this->assertSame(202, $status);
        $this->waitForJob($arch['job']['id']);
        [, $again] = $this->call('POST', "/api/crawls/$id/archive");
        $this->assertTrue($again['ready']);
        $zip = (string) file_get_contents(self::$base . "/api/crawls/$id/archive", false, $ctx);
        $this->assertStringStartsWith('PK', $zip);

        [$status] = $this->call('DELETE', "/api/crawls/$id");
        $this->assertSame(204, $status);
        [$status] = $this->call('GET', "/api/crawls/$id");
        $this->assertSame(404, $status);
    }

    /** @return array<string,mixed> */
    private function waitFor(int $id, array $statuses, int $seconds): array
    {
        $deadline = microtime(true) + $seconds;
        do {
            [, $crawl] = $this->call('GET', "/api/crawls/$id");
            if (in_array($crawl['status'] ?? '', $statuses, true)) {
                return $crawl;
            }
            usleep(400_000);
        } while (microtime(true) < $deadline);
        $this->fail('crawl did not finish: ' . json_encode($crawl));
    }

    private function waitForJob(int $id): void
    {
        for ($i = 0; $i < 60; $i++) {
            [, $job] = $this->call('GET', "/api/jobs/$id");
            if (in_array($job['status'], ['succeeded', 'failed', 'canceled', 'partial'], true)) {
                $this->assertSame('succeeded', $job['status'], json_encode($job));
                return;
            }
            usleep(300_000);
        }
        $this->fail('archive job did not finish');
    }
}
