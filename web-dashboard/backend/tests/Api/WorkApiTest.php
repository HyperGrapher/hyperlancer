<?php
declare(strict_types=1);

namespace App\Tests\Api;

use PHPUnit\Framework\TestCase;

/** Notes, to-dos, activity, contacts, links, dashboard and backups over real HTTP. */
final class WorkApiTest extends TestCase
{
    use ServerTrait;

    /** @return array<string,mixed> */
    private function site(string $url): array
    {
        [, $body] = $this->call('POST', '/api/sites', ['url' => $url, 'fetch' => false]);
        return $body['site'];
    }

    public function testNotesCrudSearchPinAndConcurrency(): void
    {
        $site = $this->site('notes-test.com');
        $id = $site['id'];
        [$status, $note] = $this->call('POST', "/api/sites/$id/notes", ['title' => 'Kickoff', 'bodyMd' => "## Goals\n- new theme"]);
        $this->assertSame(201, $status);
        [, $other] = $this->call('POST', "/api/sites/$id/notes", ['title' => 'Hosting', 'bodyMd' => 'move to vercel', 'pinned' => true]);

        [, $list] = $this->call('GET', "/api/sites/$id/notes");
        $this->assertSame(['Hosting', 'Kickoff'], array_column($list['items'], 'title'), 'pinned first');
        $this->assertArrayNotHasKey('bodyMd', $list['items'][0]);
        $this->assertStringContainsString('move to vercel', $list['items'][0]['excerpt']);
        [, $found] = $this->call('GET', "/api/sites/$id/notes?q=" . rawurlencode('new theme'));
        $this->assertSame(['Kickoff'], array_column($found['items'], 'title'));
        [, $none] = $this->call('GET', "/api/sites/$id/notes?q=" . rawurlencode('%'));
        $this->assertSame([], $none['items'], '% is literal');

        usleep(5000);
        [$status, $saved] = $this->call('PATCH', '/api/notes/' . $note['id'], ['bodyMd' => 'changed', 'updatedAt' => $note['updatedAt']]);
        $this->assertSame([200, 'changed'], [$status, $saved['bodyMd']]);
        $this->assertNotSame($note['updatedAt'], $saved['updatedAt']);
        // A second editor still holding the old timestamp must not overwrite the newer text.
        [$status, $stale] = $this->call('PATCH', '/api/notes/' . $note['id'], ['bodyMd' => 'lost update', 'updatedAt' => $note['updatedAt']]);
        $this->assertSame([409, 'conflict'], [$status, $stale['error']['code']]);
        $this->assertSame('changed', $stale['error']['current']['bodyMd']);
        [$status] = $this->call('PATCH', '/api/notes/' . $note['id'], ['bodyMd' => 'no timestamp']);
        $this->assertSame(409, $status);

        // Pinning needs no timestamp and does not count as an edit.
        [$status, $pinned] = $this->call('PATCH', '/api/notes/' . $note['id'], ['pinned' => true]);
        $this->assertSame([200, $saved['updatedAt']], [$status, $pinned['updatedAt']]);
        [$status] = $this->call('POST', "/api/sites/$id/notes", ['title' => '']);
        $this->assertSame(422, $status);
        [$status] = $this->call('POST', "/api/sites/$id/notes", ['title' => 'Big', 'bodyMd' => str_repeat('x', 200001)]);
        $this->assertSame(413, $status);

        [, $activity] = $this->call('GET', "/api/sites/$id/activity");
        $types = array_column($activity['items'], 'type');
        $this->assertSame(1, count(array_keys($types, 'note.updated', true)), 'edits are logged once per half hour');
        $this->call('DELETE', '/api/notes/' . $other['id']);
        [$status] = $this->call('GET', '/api/notes/' . $other['id']);
        $this->assertSame(404, $status);
    }

    public function testTodosLifecycleOrderingAndGlobalFilters(): void
    {
        $a = $this->site('todo-a.com');
        $b = $this->site('todo-b.com');
        $today = gmdate('Y-m-d');
        $mk = fn (int $site, array $body) => $this->call('POST', "/api/sites/$site/todos", $body)[1];

        $late = $mk($a['id'], ['title' => 'Late', 'dueDate' => gmdate('Y-m-d', strtotime('-3 days')), 'priority' => 'high']);
        $soon = $mk($a['id'], ['title' => 'Soon', 'dueDate' => gmdate('Y-m-d', strtotime('+3 days'))]);
        $later = $mk($b['id'], ['title' => 'Later', 'dueDate' => gmdate('Y-m-d', strtotime('+30 days')), 'detail' => 'with a note']);
        $nodate = $mk($b['id'], ['title' => 'Someday', 'priority' => 'low']);
        $this->assertSame('normal', $soon['priority']);
        $this->assertFalse($soon['done']);

        [$status] = $this->call('POST', "/api/sites/{$a['id']}/todos", ['title' => 'x', 'dueDate' => '2026-02-30']);
        $this->assertSame(422, $status);
        [$status] = $this->call('POST', "/api/sites/{$a['id']}/todos", ['title' => 'x', 'priority' => 'urgent']);
        $this->assertSame(422, $status);

        $titles = fn (string $qs) => array_column($this->call('GET', "/api/todos?$qs")[1]['items'], 'title');
        $mine = static fn (array $titles) => array_values(array_intersect($titles, ['Late', 'Soon', 'Later', 'Someday']));
        $this->assertSame(['Late', 'Soon', 'Later', 'Someday'], $mine($titles("today=$today")), 'by due date, undated last');
        $this->assertSame(['Late'], $mine($titles("due=overdue&today=$today")));
        $this->assertSame(['Soon'], $mine($titles("due=week&today=$today")));
        $this->assertSame(['Someday'], $mine($titles('due=none')));
        $this->assertSame(['Late'], $mine($titles('priority=high')));
        $this->assertSame(['Later', 'Someday'], $mine($titles("site={$b['id']}")));

        // Completing sets done_at, reopening clears it, both are logged.
        [, $done] = $this->call('PATCH', '/api/todos/' . $late['id'], ['done' => true]);
        $this->assertTrue($done['done']);
        $this->assertNotNull($done['doneAt']);
        $this->assertNotContains('Late', $titles("today=$today"));
        $this->assertContains('Late', $titles('status=done'));
        [, $reopened] = $this->call('PATCH', '/api/todos/' . $late['id'], ['done' => false]);
        $this->assertNull($reopened['doneAt']);
        [, $log] = $this->call('GET', "/api/sites/{$a['id']}/activity");
        $this->assertSame(['todo.reopened', 'todo.completed', 'todo.created', 'todo.created', 'site.created'], array_column($log['items'], 'type'));

        // Reordering with a fractional sort order between two neighbours.
        [, $list] = $this->call('GET', "/api/sites/{$b['id']}/todos");
        $first = $list['items'][0];
        [, $moved] = $this->call('PATCH', '/api/todos/' . $nodate['id'], ['sortOrder' => $first['sortOrder'] - 0.5]);
        [, $list] = $this->call('GET', "/api/sites/{$b['id']}/todos");
        $this->assertSame(['Someday', 'Later'], array_column($list['items'], 'title'));

        $this->call('DELETE', '/api/todos/' . $later['id']);
        [$status] = $this->call('PATCH', '/api/todos/' . $later['id'], ['done' => true]);
        $this->assertSame(404, $status);
    }

    public function testActivityPaginationAndGlobalFeed(): void
    {
        $site = $this->site('activity-test.com');
        for ($i = 1; $i <= 5; $i++) {
            $this->call('POST', "/api/sites/{$site['id']}/todos", ['title' => "T$i"]);
        }
        [, $page1] = $this->call('GET', "/api/sites/{$site['id']}/activity?limit=4");
        $this->assertCount(4, $page1['items']);
        $this->assertNotNull($page1['nextBefore']);
        [, $page2] = $this->call('GET', "/api/sites/{$site['id']}/activity?limit=4&before=" . $page1['nextBefore']);
        $this->assertCount(2, $page2['items']);
        $this->assertNull($page2['nextBefore']);
        $this->assertLessThan(end($page1['items'])['id'], $page2['items'][0]['id']);
        [, $global] = $this->call('GET', '/api/activity?limit=3');
        $this->assertCount(3, $global['items']);
        $this->assertSame('activity-test.com', $global['items'][0]['siteKey']);
    }

    public function testContactsAndLinks(): void
    {
        $site = $this->site('contacts-test.com');
        $id = $site['id'];
        [$status, $c] = $this->call('POST', "/api/sites/$id/contacts", ['name' => 'Ali K.', 'role' => 'Owner', 'email' => 'ali@example.com', 'phone' => '+90 532 111 22 33', 'preferredChannel' => 'whatsapp']);
        $this->assertSame([201, 'whatsapp'], [$status, $c['preferredChannel']]);
        foreach ([['name' => ''], ['name' => 'X', 'email' => 'nope'], ['name' => 'X', 'phone' => 'call me'], ['name' => 'X', 'preferredChannel' => 'pigeon']] as $bad) {
            [$status] = $this->call('POST', "/api/sites/$id/contacts", $bad);
            $this->assertSame(422, $status, json_encode($bad));
        }
        [, $updated] = $this->call('PATCH', '/api/contacts/' . $c['id'], ['role' => 'CEO']);
        $this->assertSame(['CEO', 'Ali K.'], [$updated['role'], $updated['name']], 'partial update keeps the rest');

        [$status, $link] = $this->call('POST', "/api/sites/$id/links", ['kind' => 'repo', 'label' => 'GitHub', 'url' => 'github.com/me/site']);
        $this->assertSame([201, 'https://github.com/me/site'], [$status, $link['url']]);
        [$status] = $this->call('POST', "/api/sites/$id/links", ['kind' => 'repo', 'url' => 'javascript:alert(1)']);
        $this->assertSame(422, $status);
        [$status] = $this->call('POST', "/api/sites/$id/links", ['kind' => 'wrong', 'url' => 'https://x.com']);
        $this->assertSame(422, $status);

        [, $detail] = $this->call('GET', "/api/sites/$id");
        $this->assertSame(['Ali K.'], array_column($detail['contacts'], 'name'));
        $this->assertSame(['GitHub'], array_column($detail['links'], 'label'));
        $this->call('DELETE', '/api/contacts/' . $c['id']);
        $this->call('DELETE', '/api/links/' . $link['id']);
        [, $detail] = $this->call('GET', "/api/sites/$id");
        $this->assertSame([[], []], [$detail['contacts'], $detail['links']]);
    }

    public function testDashboardSummarisesTheWork(): void
    {
        $site = $this->site('dash-test.com');
        $today = gmdate('Y-m-d');
        $this->call('PATCH', '/api/sites/' . $site['id'], ['status' => 'design', 'deadline' => gmdate('Y-m-d', strtotime('+5 days'))]);
        $this->call('POST', "/api/sites/{$site['id']}/todos", ['title' => 'Due today', 'dueDate' => $today, 'priority' => 'high']);
        $this->call('POST', "/api/sites/{$site['id']}/todos", ['title' => 'Far away', 'dueDate' => gmdate('Y-m-d', strtotime('+40 days'))]);
        [$status, $d] = $this->call('GET', "/api/dashboard?today=$today");
        $this->assertSame(200, $status);
        $this->assertGreaterThanOrEqual(1, $d['statusCounts']['design']);
        $this->assertContains('dash-test.com', array_column($d['deadlines'], 'siteKey'));
        $this->assertContains('Due today', array_column($d['openTodos'], 'title'));
        $this->assertNotContains('Far away', array_column($d['openTodos'], 'title'));
        $this->assertGreaterThanOrEqual(2, $d['openTodosTotal']);
        $this->assertSame(['tls', 'domains'], array_keys($d['expiring']));
        $this->assertNotEmpty($d['activity']);
        foreach (['recentFetches', 'jobs', 'siteCount'] as $key) {
            $this->assertArrayHasKey($key, $d);
        }
    }

    public function testFaviconIsFetchedServerSideAndOnlyIfItIsAnImage(): void
    {
        $dir = \App\Tests\TempDir::create();
        file_put_contents($dir . '/fav.png', base64_decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=='));
        file_put_contents($dir . '/page.png', '<html>not an image</html>');
        file_put_contents($dir . '/evil.svg', '<svg xmlns="http://www.w3.org/2000/svg"><script>alert(1)</script></svg>');
        $port = random_int(41000, 49000);
        $null = PHP_OS_FAMILY === 'Windows' ? 'NUL' : '/dev/null';
        $server = proc_open([PHP_BINARY, '-S', "127.0.0.1:$port", '-t', $dir], [['file', $null, 'r'], ['file', $null, 'w'], ['file', $null, 'w']], $pipes);
        try {
            for ($i = 0; $i < 50 && !@fsockopen('127.0.0.1', $port); $i++) {
                usleep(100_000);
            }
            [, $body] = $this->call('POST', '/api/sites', ['url' => "http://127.0.0.1:$port/", 'allowPrivateNet' => true, 'fetch' => false]);
            $id = $body['site']['id'];
            $pdo = \App\Infra\Database::open(new \App\Infra\Paths(self::$data));
            $set = fn (string $url) => $pdo->prepare('UPDATE sites SET favicon_url = ? WHERE id = ?')->execute([$url, $id]);
            $fetch = function () use ($id) {
                $raw = (string) @file_get_contents(self::$base . "/api/sites/$id/favicon", false, stream_context_create(['http' => ['header' => 'Host: localhost', 'ignore_errors' => true]]));
                preg_match('#HTTP/\S+ (\d+)#', $http_response_header[0] ?? '', $m);
                $type = '';
                foreach ($http_response_header ?? [] as $h) {
                    if (stripos($h, 'Content-Type:') === 0) {
                        $type = trim(substr($h, 13));
                    }
                }
                return [(int) ($m[1] ?? 0), $type, $raw];
            };

            [$status] = $fetch();
            $this->assertSame(404, $status, 'no favicon recorded yet');
            $set("http://127.0.0.1:$port/fav.png");
            [$status, $type, $raw] = $fetch();
            $this->assertSame([200, 'image/png'], [$status, $type]);
            $this->assertStringStartsWith("\x89PNG", $raw);
            $set("http://127.0.0.1:$port/page.png");
            [$status] = $fetch();
            $this->assertSame(404, $status, 'HTML served as an image is refused');
            $set("http://127.0.0.1:$port/evil.svg");
            [$status, $type] = $fetch();
            $this->assertSame([200, 'image/svg+xml'], [$status, $type]);
            // Without permission for private networks the same address is refused.
            $pdo->prepare('UPDATE sites SET allow_private_net = 0, favicon_url = ? WHERE id = ?')->execute(["http://127.0.0.1:$port/fav.png", $id]);
            [$status] = $fetch();
            $this->assertSame(404, $status);
        } finally {
            proc_terminate($server);
            proc_close($server);
            \App\Tests\TempDir::remove($dir);
        }
    }

    public function testBackupsCreateListAndSafeDownload(): void
    {
        [$status, $backup] = $this->call('POST', '/api/backups');
        $this->assertSame(201, $status);
        [, $list] = $this->call('GET', '/api/backups');
        $this->assertContains($backup['file'], array_column($list['items'], 'file'));

        $raw = (string) file_get_contents(self::$base . '/api/backups/' . rawurlencode($backup['file']) . '/download', false, stream_context_create(['http' => ['header' => 'Host: localhost', 'ignore_errors' => true]]));
        $this->assertStringStartsWith('SQLite format 3', $raw);
        foreach (['../app.sqlite', '..%2Fapp.sqlite', 'app.sqlite', 'notes.txt', 'app-1-x.sqlite.php'] as $evil) {
            [$status] = $this->call('GET', '/api/backups/' . $evil . '/download');
            $this->assertContains($status, [404, 405], $evil);
        }
    }
}
