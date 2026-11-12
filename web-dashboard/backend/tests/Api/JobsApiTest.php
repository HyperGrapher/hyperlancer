<?php
declare(strict_types=1);

namespace App\Tests\Api;

use PHPUnit\Framework\TestCase;

/** Boots `php -S` on a temp data dir and drives the real HTTP API and worker. */
final class JobsApiTest extends TestCase
{
    use ServerTrait;

    public function testHealthIsOkAndMigrated(): void
    {
        [$status, $body] = $this->call('GET', '/api/health');
        $this->assertSame(200, $status);
        $this->assertSame(2, $body['migrations']['current']);
        $this->assertTrue($body['env']['dataDirWritable']);
        $this->assertTrue($body['env']['phpCli']['ok']);
    }

    public function testMutationsAreGuarded(): void
    {
        [$status] = $this->call('POST', '/api/system/spawn-test', ['seconds' => 5], ['X-Requested-With: nope']);
        $this->assertSame(403, $status);
        [$status] = $this->call('GET', '/api/health', null, ['Host: evil.example']);
        $this->assertSame(421, $status);
    }

    public function testSelfcheckJobRunsToCompletion(): void
    {
        [$status, $body] = $this->call('POST', '/api/system/spawn-test', ['seconds' => 5]);
        $this->assertSame(202, $status);
        $id = $body['job']['id'];
        $job = $this->waitFor($id, ['succeeded', 'failed', 'canceled'], 25);
        $this->assertSame('succeeded', $job['status'], json_encode($job));
        $this->assertSame(5, $job['progress']['current']);
    }

    public function testRunningJobCanBeCanceled(): void
    {
        [, $body] = $this->call('POST', '/api/system/spawn-test', ['seconds' => 60]);
        $id = $body['job']['id'];
        $this->waitFor($id, ['running'], 10);
        [$status] = $this->call('POST', "/api/jobs/$id/cancel");
        $this->assertSame(202, $status);
        $job = $this->waitFor($id, ['canceled', 'failed', 'succeeded'], 15);
        $this->assertSame('canceled', $job['status']);
    }

    public function testQueuedJobCancelsImmediately(): void
    {
        // The selfcheck limit is 3, so the fourth job stays queued while three run.
        $ids = [];
        for ($i = 0; $i < 4; $i++) {
            [, $body] = $this->call('POST', '/api/system/spawn-test', ['seconds' => 30]);
            $ids[] = $body['job']['id'];
        }
        [, $job] = $this->call('POST', "/api/jobs/{$ids[3]}/cancel");
        $this->assertSame('canceled', $job['status']);
        foreach ($ids as $id) {
            $this->call('POST', "/api/jobs/$id/cancel");
        }
        foreach (array_slice($ids, 0, 3) as $id) {
            $this->waitFor($id, ['canceled', 'failed'], 15);
        }
    }

    /** @param list<string> $statuses @return array<string,mixed> */
    private function waitFor(int $id, array $statuses, int $seconds): array
    {
        $deadline = microtime(true) + $seconds;
        do {
            [, $job] = $this->call('GET', "/api/jobs/$id");
            if (in_array($job['status'] ?? '', $statuses, true)) {
                return $job;
            }
            usleep(300_000);
        } while (microtime(true) < $deadline);
        $this->fail("job $id did not reach " . implode('/', $statuses) . ': ' . json_encode($job));
    }
}
