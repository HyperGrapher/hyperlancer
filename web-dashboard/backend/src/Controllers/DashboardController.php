<?php
declare(strict_types=1);

namespace App\Controllers;

use App\App;
use App\Domain\SiteQuery;
use App\Http\Request;
use App\Http\Response;
use App\Jobs\JobRepository;
use PDO;

final class DashboardController
{
    private readonly PDO $pdo;

    public function __construct(private readonly App $app)
    {
        $this->pdo = $app->pdo();
    }

    public function get(Request $request): Response
    {
        $this->app->launcher()->ensureWorker(); // the dashboard is what you look at while jobs run
        $today = is_string($request->query['today'] ?? null) && preg_match('/^\d{4}-\d{2}-\d{2}$/', $request->query['today']) ? $request->query['today'] : gmdate('Y-m-d');
        $plus = static fn (int $days) => gmdate('Y-m-d', strtotime("$today +$days days"));

        $counts = array_fill_keys(SiteQuery::STATUSES, 0);
        foreach ($this->pdo->query('SELECT status, count(*) AS n FROM sites WHERE archived_at IS NULL GROUP BY status')->fetchAll() as $r) {
            $counts[$r['status']] = (int) $r['n'];
        }

        $site = static fn ($r) => ['id' => (int) $r['id'], 'siteKey' => $r['site_key'], 'name' => $r['name']];
        $rows = function (string $sql, array $args = []) {
            $stmt = $this->pdo->prepare($sql);
            $stmt->execute($args);
            return $stmt->fetchAll();
        };

        $deadlines = array_map(static fn ($r) => $site($r) + ['deadline' => $r['deadline'], 'status' => $r['status']], $rows(
            "SELECT id, site_key, name, deadline, status FROM sites WHERE archived_at IS NULL AND deadline IS NOT NULL AND deadline <= ?
               AND status NOT IN ('done','canceled') ORDER BY deadline LIMIT 10",
            [$plus(14)]
        ));

        $todos = array_map(static fn ($r) => [
            'id' => (int) $r['id'], 'siteId' => (int) $r['site_id'], 'title' => $r['title'], 'done' => false, 'dueDate' => $r['due_date'],
            'priority' => $r['priority'], 'site' => ['id' => (int) $r['site_id'], 'siteKey' => $r['site_key'], 'name' => $r['site_name']],
        ], $rows(
            'SELECT t.*, s.site_key, s.name AS site_name FROM todos t JOIN sites s ON s.id = t.site_id
              WHERE t.done = 0 AND s.archived_at IS NULL AND t.due_date IS NOT NULL AND t.due_date <= ?
              ORDER BY t.due_date, CASE t.priority WHEN \'high\' THEN 0 WHEN \'normal\' THEN 1 ELSE 2 END LIMIT 12',
            [$plus(7)]
        ));
        $openTotal = (int) $this->pdo->query('SELECT count(*) FROM todos t JOIN sites s ON s.id = t.site_id WHERE t.done = 0 AND s.archived_at IS NULL')->fetchColumn();

        $expiring = [
            'tls' => array_map(static fn ($r) => $site($r) + ['date' => $r['tls_valid_until']], $rows(
                'SELECT id, site_key, name, tls_valid_until FROM sites WHERE archived_at IS NULL AND tls_valid_until IS NOT NULL AND tls_valid_until <= ? ORDER BY tls_valid_until LIMIT 8', [$plus(30)])),
            'domains' => array_map(static fn ($r) => $site($r) + ['date' => $r['domain_expires_on']], $rows(
                'SELECT id, site_key, name, domain_expires_on FROM sites WHERE archived_at IS NULL AND domain_expires_on IS NOT NULL AND domain_expires_on <= ? ORDER BY domain_expires_on LIMIT 8', [$plus(60)])),
        ];

        $recent = array_map(static fn ($r) => $site($r) + ['fetchedAt' => $r['last_fetched_at'], 'outcome' => $r['last_fetch_outcome'], 'cms' => $r['cms']], $rows(
            'SELECT id, site_key, name, last_fetched_at, last_fetch_outcome, cms FROM sites WHERE archived_at IS NULL AND last_fetched_at IS NOT NULL ORDER BY last_fetched_at DESC LIMIT 8'
        ));

        $jobs = array_map([JobRepository::class, 'toApi'], $rows(
            "SELECT * FROM jobs WHERE status IN ('queued','running') OR finished_at > strftime('%Y-%m-%dT%H:%M:%fZ','now','-1 day') ORDER BY (status IN ('queued','running')) DESC, id DESC LIMIT 6"
        ));

        $activity = array_map(static fn ($r) => [
            'id' => (int) $r['id'], 'siteId' => $r['site_id'] === null ? null : (int) $r['site_id'], 'siteKey' => $r['site_key'],
            'type' => $r['type'], 'message' => $r['message'], 'createdAt' => $r['created_at'],
        ], $rows('SELECT a.*, s.site_key FROM activity a LEFT JOIN sites s ON s.id = a.site_id ORDER BY a.id DESC LIMIT 15'));

        return Response::json([
            'today' => $today, 'statusCounts' => $counts, 'siteCount' => array_sum($counts), 'deadlines' => $deadlines,
            'openTodos' => $todos, 'openTodosTotal' => $openTotal, 'expiring' => $expiring, 'recentFetches' => $recent,
            'jobs' => $jobs, 'activity' => $activity,
        ]);
    }
}
