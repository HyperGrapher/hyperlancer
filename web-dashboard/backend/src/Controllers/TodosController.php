<?php
declare(strict_types=1);

namespace App\Controllers;

use App\App;
use App\Domain\ActivityLogger;
use App\Domain\SiteService;
use App\Http\HttpException;
use App\Http\Request;
use App\Http\Response;
use PDO;

final class TodosController
{
    private readonly PDO $pdo;
    private readonly ActivityLogger $activity;

    public function __construct(App $app)
    {
        $this->pdo = $app->pdo();
        $this->activity = new ActivityLogger($this->pdo);
    }

    /** @param array<string,string> $p */
    public function forSite(Request $request, array $p): Response
    {
        $siteId = (int) $p['id'];
        (new SiteService($this->pdo))->requireRow($siteId);
        $stmt = $this->pdo->prepare('SELECT * FROM todos WHERE site_id = ? ORDER BY done ASC, CASE WHEN done = 1 THEN 0 ELSE sort_order END ASC, done_at DESC, id ASC');
        $stmt->execute([$siteId]);
        return Response::json(['items' => array_map(fn ($r) => $this->shape($r), $stmt->fetchAll())]);
    }

    /** Global list, grouped by the client into Overdue / Today / This week / Later / No date. */
    public function all(Request $request): Response
    {
        $q = $request->query;
        $today = $this->today($q['today'] ?? null);
        $where = ['s.archived_at IS NULL'];
        $args = [];
        $status = (string) ($q['status'] ?? 'open');
        if ($status === 'open') {
            $where[] = 't.done = 0';
        } elseif ($status === 'done') {
            $where[] = 't.done = 1';
        }
        if (isset($q['site']) && ctype_digit((string) $q['site'])) {
            $where[] = 't.site_id = ?';
            $args[] = (int) $q['site'];
        }
        if (in_array($q['priority'] ?? '', ['low', 'normal', 'high'], true)) {
            $where[] = 't.priority = ?';
            $args[] = $q['priority'];
        }
        $week = gmdate('Y-m-d', strtotime($today . ' +7 days'));
        switch ($q['due'] ?? '') {
            case 'overdue':
                $where[] = 't.done = 0 AND t.due_date < ?';
                $args[] = $today;
                break;
            case 'today':
                $where[] = 't.due_date = ?';
                $args[] = $today;
                break;
            case 'week':
                $where[] = 't.due_date >= ? AND t.due_date <= ?';
                array_push($args, $today, $week);
                break;
            case 'none':
                $where[] = 't.due_date IS NULL';
                break;
        }
        $sql = 'FROM todos t JOIN sites s ON s.id = t.site_id WHERE ' . implode(' AND ', $where);
        $total = $this->pdo->prepare("SELECT count(*) $sql");
        $total->execute($args);
        $stmt = $this->pdo->prepare(
            "SELECT t.*, s.site_key, s.name AS site_name $sql ORDER BY t.done ASC, (t.due_date IS NULL) ASC, t.due_date ASC,
                CASE t.priority WHEN 'high' THEN 0 WHEN 'normal' THEN 1 ELSE 2 END, t.id ASC LIMIT 500"
        );
        $stmt->execute($args);
        return Response::json([
            'items' => array_map(fn ($r) => $this->shape($r) + ['site' => ['id' => (int) $r['site_id'], 'siteKey' => $r['site_key'], 'name' => $r['site_name']]], $stmt->fetchAll()),
            'total' => (int) $total->fetchColumn(),
        ]);
    }

    /** @param array<string,string> $p */
    public function create(Request $request, array $p): Response
    {
        $siteId = (int) $p['id'];
        (new SiteService($this->pdo))->requireRow($siteId);
        $b = $request->json();
        $title = $this->title($b['title'] ?? null);
        $max = $this->pdo->prepare('SELECT COALESCE(max(sort_order), 0) FROM todos WHERE site_id = ?');
        $max->execute([$siteId]);
        $this->pdo->prepare('INSERT INTO todos (site_id, title, detail, due_date, priority, sort_order) VALUES (?, ?, ?, ?, ?, ?)')->execute([
            $siteId, $title, $this->detail($b['detail'] ?? null), $this->date($b['dueDate'] ?? null), $this->priority($b['priority'] ?? 'normal'), (float) $max->fetchColumn() + 1,
        ]);
        $id = (int) $this->pdo->lastInsertId();
        $this->activity->log($siteId, 'todo.created', "To-do added: $title", ['todoId' => $id]);
        return Response::json($this->shape($this->row($id)), 201);
    }

    /** @param array<string,string> $p */
    public function patch(Request $request, array $p): Response
    {
        $id = (int) $p['id'];
        $row = $this->row($id);
        $b = $request->json();
        $set = [];
        $args = [];
        if (array_key_exists('title', $b)) {
            $set[] = 'title = ?';
            $args[] = $this->title($b['title']);
        }
        if (array_key_exists('detail', $b)) {
            $set[] = 'detail = ?';
            $args[] = $this->detail($b['detail']);
        }
        if (array_key_exists('dueDate', $b)) {
            $set[] = 'due_date = ?';
            $args[] = $this->date($b['dueDate']);
        }
        if (array_key_exists('priority', $b)) {
            $set[] = 'priority = ?';
            $args[] = $this->priority($b['priority']);
        }
        if (array_key_exists('sortOrder', $b)) {
            if (!is_numeric($b['sortOrder'])) {
                throw new HttpException(422, 'validation_failed', 'Invalid sort order.', ['sortOrder' => 'Must be a number.']);
            }
            $set[] = 'sort_order = ?';
            $args[] = (float) $b['sortOrder'];
        }
        $done = array_key_exists('done', $b) ? (bool) $b['done'] : null;
        if ($done !== null && $done !== (bool) $row['done']) {
            $set[] = 'done = ?';
            $args[] = $done ? 1 : 0;
            $set[] = $done ? "done_at = strftime('%Y-%m-%dT%H:%M:%fZ','now')" : 'done_at = NULL';
        }
        if ($set) {
            $this->pdo->prepare('UPDATE todos SET ' . implode(', ', $set) . ", updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id = ?")->execute([...$args, $id]);
        }
        if ($done !== null && $done !== (bool) $row['done']) {
            $this->activity->log((int) $row['site_id'], $done ? 'todo.completed' : 'todo.reopened', ($done ? 'Completed: ' : 'Reopened: ') . $row['title'], ['todoId' => $id]);
        }
        return Response::json($this->shape($this->row($id)));
    }

    /** @param array<string,string> $p */
    public function delete(Request $request, array $p): Response
    {
        $row = $this->row((int) $p['id']);
        $this->pdo->prepare('DELETE FROM todos WHERE id = ?')->execute([$row['id']]);
        $this->activity->log((int) $row['site_id'], 'todo.deleted', "To-do deleted: {$row['title']}");
        return Response::noContent();
    }

    /** @return array<string,mixed> */
    private function row(int $id): array
    {
        $stmt = $this->pdo->prepare('SELECT * FROM todos WHERE id = ?');
        $stmt->execute([$id]);
        return $stmt->fetch() ?: throw new HttpException(404, 'not_found', 'To-do not found.');
    }

    /** @param array<string,mixed> $r @return array<string,mixed> */
    private function shape(array $r): array
    {
        return [
            'id' => (int) $r['id'], 'siteId' => (int) $r['site_id'], 'title' => $r['title'], 'detail' => $r['detail'],
            'done' => (bool) $r['done'], 'doneAt' => $r['done_at'], 'dueDate' => $r['due_date'], 'priority' => $r['priority'],
            'sortOrder' => (float) $r['sort_order'], 'createdAt' => $r['created_at'], 'updatedAt' => $r['updated_at'],
        ];
    }

    private function title(mixed $value): string
    {
        $title = is_string($value) ? trim($value) : '';
        if ($title === '' || mb_strlen($title) > 300) {
            throw new HttpException(422, 'validation_failed', 'A to-do needs a title of up to 300 characters.', ['title' => '1 to 300 characters.']);
        }
        return $title;
    }

    private function detail(mixed $value): ?string
    {
        $detail = $value === null ? '' : trim((string) $value);
        if (mb_strlen($detail) > 2000) {
            throw new HttpException(422, 'validation_failed', 'The note on a to-do is limited to 2,000 characters.', ['detail' => 'Too long.']);
        }
        return $detail === '' ? null : $detail;
    }

    private function date(mixed $value): ?string
    {
        if ($value === null || $value === '') {
            return null;
        }
        if (!is_string($value) || !preg_match('/^(\d{4})-(\d{2})-(\d{2})$/', $value, $m) || !checkdate((int) $m[2], (int) $m[3], (int) $m[1])) {
            throw new HttpException(422, 'validation_failed', 'Invalid due date.', ['dueDate' => 'Use the format YYYY-MM-DD.']);
        }
        return $value;
    }

    private function priority(mixed $value): string
    {
        if (!in_array($value, ['low', 'normal', 'high'], true)) {
            throw new HttpException(422, 'validation_failed', 'Invalid priority.', ['priority' => 'Use low, normal or high.']);
        }
        return $value;
    }

    private function today(mixed $value): string
    {
        return is_string($value) && preg_match('/^\d{4}-\d{2}-\d{2}$/', $value) ? $value : gmdate('Y-m-d');
    }
}
