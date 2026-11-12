<?php
declare(strict_types=1);

namespace App\Tests\Api;

use App\Domain\ActivityLogger;
use App\Domain\ProfileIngestor;
use App\Domain\ProfileNormalizer;
use App\Domain\SnapshotDiff;
use App\Infra\Database;
use App\Infra\Paths;
use PHPUnit\Framework\TestCase;

/** Sites, taxonomy, snapshots and bulk actions over real HTTP. Profile jobs are not started (fetch:false). */
final class SitesApiTest extends TestCase
{
    use ServerTrait;

    private function ingestor(): ProfileIngestor
    {
        $pdo = Database::open(new Paths(self::$data));
        return new ProfileIngestor($pdo, new ProfileNormalizer(), new SnapshotDiff(), new ActivityLogger($pdo));
    }

    /** @return array<string,mixed> */
    private function site(string $url, array $extra = []): array
    {
        [$status, $body] = $this->call('POST', '/api/sites', ['url' => $url, 'fetch' => false] + $extra);
        $this->assertSame(201, $status, json_encode($body));
        $this->assertNull($body['job']);
        return $body['site'];
    }

    public function testCreateNormalizesAndRejectsBadInput(): void
    {
        $site = $this->site('WWW.Create-Test.com/path', ['name' => 'Create test', 'status' => 'design', 'tags' => ['alpha', 'Alpha', 'beta']]);
        $this->assertSame('create-test.com', $site['siteKey']);
        $this->assertSame('https://www.create-test.com/path', $site['url']);
        $this->assertSame('design', $site['status']);
        $this->assertSame(['alpha', 'beta'], array_column($site['tags'], 'name'));

        [$status, $body] = $this->call('POST', '/api/sites', ['url' => 'ftp://x.com']);
        $this->assertSame([422, 'validation_failed'], [$status, $body['error']['code']]);
        $this->assertArrayHasKey('url', $body['error']['fields']);
        [$status, $body] = $this->call('POST', '/api/sites', ['url' => 'http://10.0.0.5/admin']);
        $this->assertSame([422, 'blocked'], [$status, $body['error']['code']]);
        [$status] = $this->call('POST', '/api/sites', ['url' => '10.0.0.5', 'allowPrivateNet' => true, 'fetch' => false]);
        $this->assertSame(201, $status);
        [$status, $body] = $this->call('POST', '/api/sites', ['url' => 'create-test.com', 'status' => 'nope']);
        $this->assertSame(409, $status, 'duplicate wins over validation of later fields'); // duplicate is detected first
        $this->assertSame('create-test.com', $body['error']['existing']['siteKey']);
    }

    public function testDuplicatesCheckUrlAndSeparateProjects(): void
    {
        $this->site('dup-test.com');
        [, $check] = $this->call('GET', '/api/sites/check-url?url=' . rawurlencode('www.dup-test.com'));
        $this->assertSame('dup-test.com', $check['existing']['siteKey']);
        $this->site('shop.dup-test.com');
        [, $check] = $this->call('GET', '/api/sites/check-url?url=dup-test.com');
        $this->assertSame(['shop.dup-test.com'], array_column($check['related'], 'siteKey'));
        [$status, $body] = $this->call('POST', '/api/sites', ['url' => 'dup-test.com', 'allowDuplicate' => true, 'fetch' => false]);
        $this->assertSame([201, 'dup-test.com#2'], [$status, $body['site']['siteKey']]);
    }

    public function testPatchWritesActivityAndValidates(): void
    {
        $site = $this->site('patch-test.com');
        [$status, $body] = $this->call('PATCH', '/api/sites/' . $site['id'], ['status' => 'waiting', 'tags' => ['x', 'y'], 'deadline' => '2026-12-24', 'name' => 'Patch']);
        $this->assertSame(200, $status);
        $this->assertSame(['waiting', '2026-12-24', 'Patch'], [$body['status'], $body['deadline'], $body['name']]);
        [$status] = $this->call('PATCH', '/api/sites/' . $site['id'], ['deadline' => '2026-02-31']);
        $this->assertSame(422, $status);
        [$status] = $this->call('PATCH', '/api/sites/' . $site['id'], ['categoryId' => 9999]);
        $this->assertSame(422, $status);
        [$status] = $this->call('PATCH', '/api/sites/99999', ['name' => 'x']);
        $this->assertSame(404, $status);

        $pdo = Database::open(new \App\Infra\Paths(self::$data));
        $messages = $pdo->query('SELECT message FROM activity WHERE site_id = ' . (int) $site['id'] . ' ORDER BY id')->fetchAll(\PDO::FETCH_COLUMN);
        $this->assertSame(['Added patch-test.com', 'Status: none → waiting', 'Tags: +x +y'], $messages);
    }

    public function testPreviewUrlIsValidatedAndStored(): void
    {
        $site = $this->site('preview-test.com');
        $this->assertNull($site['previewUrl']);
        [$status, $body] = $this->call('PATCH', '/api/sites/' . $site['id'], ['previewUrl' => 'preview-test.vercel.app']);
        $this->assertSame([200, 'https://preview-test.vercel.app'], [$status, $body['previewUrl']]);
        [$status] = $this->call('PATCH', '/api/sites/' . $site['id'], ['previewUrl' => 'http://exa mple']);
        $this->assertSame(422, $status);
        [, $body] = $this->call('PATCH', '/api/sites/' . $site['id'], ['previewUrl' => '']);
        $this->assertNull($body['previewUrl'], 'an empty value clears it');
    }

    public function testFiltersFacetsSortAndPagination(): void
    {
        [, $cat] = $this->call('POST', '/api/categories', ['name' => 'Food', 'color' => 'emerald']);
        $a = $this->site('filter-a.com', ['status' => 'design', 'tags' => ['seo'], 'categoryId' => $cat['id']]);
        $b = $this->site('filter-b.com', ['status' => 'waiting', 'tags' => ['seo', 'wp']]);
        $c = $this->site('filter-c.com', ['status' => 'design', 'name' => 'Zeytin Co']);
        $pdo = Database::open(new \App\Infra\Paths(self::$data));
        $pdo->exec("UPDATE sites SET cms = 'WordPress', last_fetch_outcome = 'ok', last_fetched_at = '2026-10-01T10:00:00Z' WHERE id = " . (int) $b['id']);
        $pdo->exec("UPDATE sites SET cms = 'Custom', last_fetch_outcome = 'failed', last_fetched_at = '2026-10-20T10:00:00Z' WHERE id = " . (int) $a['id']);

        $keys = fn (string $qs) => array_column($this->call('GET', "/api/sites?perPage=100&$qs")[1]['items'], 'siteKey');
        $this->assertSame(['filter-a.com', 'filter-c.com'], array_values(array_intersect($keys('status=design&sort=domain'), ['filter-a.com', 'filter-c.com'])));
        $this->assertSame(['filter-a.com', 'filter-b.com'], $keys('tag=seo&sort=domain&q=filter-'));
        $this->assertSame(['filter-b.com'], $keys('tag=seo,wp&tagMode=all&q=filter-'));
        $this->assertSame(['filter-b.com'], $keys('cms=wordpress'));
        $this->assertSame(['filter-a.com'], $keys('category=Food'));
        $this->assertSame(['filter-c.com'], $keys('q=zeytin'));
        $this->assertSame(['filter-c.com'], $keys('outcome=never&q=filter-'));
        $this->assertSame(['filter-a.com', 'filter-b.com'], $keys('outcome=ok,failed&q=filter-&sort=domain'));
        $this->assertSame(['filter-a.com'], $keys('fetched=2026-10-15..2026-10-31&q=filter-'));
        $this->assertSame(['filter-b.com', 'filter-a.com', 'filter-c.com'], $keys('q=filter-&sort=fetched,domain'), 'oldest fetch first, never-fetched last');
        $this->assertSame(['filter-a.com', 'filter-b.com', 'filter-c.com'], $keys('q=filter-&sort=-fetched,domain'), 'newest fetch first, never-fetched still last');

        [, $page] = $this->call('GET', '/api/sites?q=filter-&perPage=2&page=2&sort=domain');
        $this->assertSame([3, 2, 2], [$page['total'], $page['page'], $page['perPage']]);
        $this->assertSame(['filter-c.com'], array_column($page['items'], 'siteKey'));

        [, $res] = $this->call('GET', '/api/sites?q=filter-&status=design');
        $this->assertSame(['design' => 2, 'waiting' => 1], $res['facets']['status'], 'a facet ignores its own filter');
        $this->assertSame(['seo' => 1], $res['facets']['tag'], 'other facets obey the active filters');

        // SQL metacharacters are data, not syntax.
        [$status, $res] = $this->call('GET', '/api/sites?q=' . rawurlencode("x' OR 1=1 --") . '&sort=' . rawurlencode('updated_at; DROP TABLE sites'));
        $this->assertSame([200, 0], [$status, $res['total']]);
        [, $res] = $this->call('GET', '/api/sites?q=' . rawurlencode('%'));
        $this->assertSame(0, $res['total'], '% is matched literally');
    }

    public function testArchiveRestoreDeleteAndBulk(): void
    {
        $a = $this->site('bulk-a.com');
        $b = $this->site('bulk-b.com');
        [$status, $res] = $this->call('POST', '/api/sites/bulk', ['ids' => [$a['id'], $b['id']], 'action' => 'setStatus', 'value' => 'done']);
        $this->assertSame([200, 2], [$status, $res['updated']]);
        [, $res] = $this->call('POST', '/api/sites/bulk', ['ids' => [$a['id'], $b['id']], 'action' => 'addTag', 'value' => 'bulked']);
        [, $list] = $this->call('GET', '/api/sites?tag=bulked&status=done');
        $this->assertSame(2, $list['total']);
        $this->call('POST', '/api/sites/bulk', ['ids' => [$a['id']], 'action' => 'removeTag', 'value' => 'bulked']);
        [, $list] = $this->call('GET', '/api/sites?tag=bulked');
        $this->assertSame(['bulk-b.com'], array_column($list['items'], 'siteKey'));
        [$status] = $this->call('POST', '/api/sites/bulk', ['ids' => [$a['id']], 'action' => 'explode']);
        $this->assertSame(422, $status);
        [$status] = $this->call('POST', '/api/sites/bulk', ['ids' => [], 'action' => 'archive']);
        $this->assertSame(422, $status);

        [$status] = $this->call('DELETE', '/api/sites/' . $a['id']);
        $this->assertSame(204, $status);
        [, $list] = $this->call('GET', '/api/sites?q=bulk-a');
        $this->assertSame(0, $list['total']);
        [, $list] = $this->call('GET', '/api/sites?q=bulk-a&archived=1');
        $this->assertSame(1, $list['total']);
        [, $restored] = $this->call('POST', '/api/sites/' . $a['id'] . '/restore');
        $this->assertNull($restored['archivedAt']);
        $this->call('POST', '/api/sites/bulk', ['ids' => [$a['id'], $b['id']], 'action' => 'archive']);
        [, $list] = $this->call('GET', '/api/sites?q=bulk-&archived=1');
        $this->assertSame(2, $list['total']);
        $this->call('POST', '/api/sites/bulk', ['ids' => [$a['id'], $b['id']], 'action' => 'restore']);
        [, $list] = $this->call('GET', '/api/sites?q=bulk-&archived=1');
        $this->assertSame(0, $list['total']);

        [$status] = $this->call('DELETE', '/api/sites/' . $a['id'] . '?mode=delete&confirm=wrong');
        $this->assertSame(422, $status);
        [$status] = $this->call('DELETE', '/api/sites/' . $a['id'] . '?mode=delete&confirm=bulk-a.com');
        $this->assertSame(204, $status);
        [$status] = $this->call('GET', '/api/sites/' . $a['id']);
        $this->assertSame(404, $status);
    }

    public function testTaxonomyAndViews(): void
    {
        [$status, $cat] = $this->call('POST', '/api/categories', ['name' => 'Industry']);
        $this->assertSame(201, $status);
        [$status] = $this->call('POST', '/api/categories', ['name' => 'industry']);
        $this->assertSame(409, $status, 'names are unique case-insensitively');
        [$status, $renamed] = $this->call('PATCH', '/api/categories/' . $cat['id'], ['name' => 'Heavy industry', 'color' => 'amber']);
        $this->assertSame([200, 'Heavy industry'], [$status, $renamed['name']]);
        $site = $this->site('tax-test.com', ['categoryId' => $cat['id']]);
        $this->call('DELETE', '/api/categories/' . $cat['id']);
        [, $fresh] = $this->call('GET', '/api/sites/' . $site['id']);
        $this->assertNull($fresh['category'], 'deleting a category clears it on sites');

        $this->call('PATCH', '/api/sites/' . $site['id'], ['tags' => ['old-name', 'keep']]);
        [, $tags] = $this->call('GET', '/api/tags');
        $byName = array_column($tags['items'], null, 'name');
        [, $keep] = $this->call('POST', '/api/tags', ['name' => 'target']);
        [$status, $merged] = $this->call('POST', '/api/tags/' . $byName['old-name']['id'] . '/merge', ['intoId' => $keep['id']]);
        $this->assertSame([200, 'target'], [$status, $merged['name']]);
        [, $fresh] = $this->call('GET', '/api/sites/' . $site['id']);
        $this->assertEqualsCanonicalizing(['keep', 'target'], array_column($fresh['tags'], 'name'));

        [$status, $view] = $this->call('POST', '/api/views', ['name' => 'Waiting on client', 'query' => '?status=waiting&tag=seo']);
        $this->assertSame([201, 'status=waiting&tag=seo'], [$status, $view['query']]);
        [$status] = $this->call('POST', '/api/views', ['name' => 'waiting on client', 'query' => 'x=1']);
        $this->assertSame(409, $status);
        [, $views] = $this->call('GET', '/api/views');
        $this->assertContains('Waiting on client', array_column($views['items'], 'name'));
        [$status] = $this->call('DELETE', '/api/views/' . $view['id']);
        $this->assertSame(204, $status);
    }

    public function testSnapshotsDiffAndDeleteRepointsTheSite(): void
    {
        $site = $this->site('snap-test.com');
        $id = (int) $site['id'];
        $raw = json_decode((string) file_get_contents(__DIR__ . '/../fixtures/profiles/v2-ok.json'), true);
        $ingestor = $this->ingestor();
        $first = $ingestor->ingest($id, null, $raw);
        $raw['page']['title'] = 'A new title';
        $raw['technologies'][] = ['category' => 'Hosting / edge', 'name' => 'Cloudflare', 'confidence' => 'high', 'evidence' => ['cf-ray']];
        $second = $ingestor->ingest($id, null, $raw);
        $failed = $ingestor->ingestFailure($id, null, 'timeout');

        [, $detail] = $this->call('GET', "/api/sites/$id");
        $this->assertSame($second['snapshotId'], $detail['latestSnapshot']['id']);
        $this->assertSame($failed['snapshotId'], $detail['lastAttempt']['id']);
        $this->assertSame('failed', $detail['lastFetchOutcome']);
        $this->assertSame(3, $detail['counts']['snapshots']);

        [, $list] = $this->call('GET', "/api/sites/$id/snapshots");
        $this->assertSame([$failed['snapshotId'], $second['snapshotId'], $first['snapshotId']], array_column($list['items'], 'id'));
        [, $one] = $this->call('GET', '/api/snapshots/' . $second['snapshotId']);
        $this->assertSame('A new title', $one['summary']['seo']['title']);
        $this->assertArrayNotHasKey('raw', $one);
        // A snapshot stored before a summary field existed still serves it (re-normalised from the raw report).
        $pdo = Database::open(new \App\Infra\Paths(self::$data));
        $pdo->exec("UPDATE profile_snapshots SET summary_json = json_remove(summary_json, '$.design') WHERE id = " . (int) $second['snapshotId']);
        [, $again] = $this->call('GET', '/api/snapshots/' . $second['snapshotId']);
        $this->assertSame('2018-08', $again['summary']['design']['estimatedFrom']);
        [, $withRaw] = $this->call('GET', '/api/snapshots/' . $second['snapshotId'] . '?raw=1');
        $this->assertSame(2, $withRaw['raw']['schema_version']);

        [, $diff] = $this->call('GET', '/api/snapshots/' . $second['snapshotId'] . '/diff');
        $this->assertSame([$first['snapshotId'], $second['snapshotId']], [$diff['a']['id'], $diff['b']['id']]);
        $this->assertSame(3, $diff['count']); // title, the technology and the edge it implies
        [, $swapped] = $this->call('GET', '/api/snapshots/' . $first['snapshotId'] . '/diff?against=' . $second['snapshotId']);
        $this->assertSame($diff['count'], $swapped['count'], 'older and newer are ordered for you');
        [, $none] = $this->call('GET', '/api/snapshots/' . $first['snapshotId'] . '/diff');
        $this->assertSame([null, 0], [$none['a'], $none['count']]);

        $this->call('DELETE', '/api/snapshots/' . $second['snapshotId']);
        [, $detail] = $this->call('GET', "/api/sites/$id");
        $this->assertSame($first['snapshotId'], $detail['latestSnapshot']['id'], 'falls back to the newest good snapshot');
        [$status] = $this->call('GET', '/api/snapshots/' . $second['snapshotId']);
        $this->assertSame(404, $status);
    }

    public function testFetchEndpointQueuesOnceAndRejectsAParallelOne(): void
    {
        $site = $this->site('fetch-test.com');
        // Keep the queue inert: a queued job is enough to test the 409 rule without running the profiler.
        $pdo = Database::open(new \App\Infra\Paths(self::$data));
        $pdo->exec("INSERT INTO jobs (type, site_id, status, params_json) VALUES ('profile', {$site['id']}, 'running', '{}')");
        [$status, $body] = $this->call('POST', '/api/sites/' . $site['id'] . '/fetch');
        $this->assertSame([409, 'conflict'], [$status, $body['error']['code']]);
        $pdo->exec("UPDATE jobs SET status = 'succeeded' WHERE site_id = {$site['id']}");
        [$status, $body] = $this->call('POST', '/api/sites/' . $site['id'] . '/fetch');
        $this->assertSame([202, 'profile'], [$status, $body['job']['type']]);
        $this->call('POST', '/api/jobs/' . $body['job']['id'] . '/cancel');
    }
}
