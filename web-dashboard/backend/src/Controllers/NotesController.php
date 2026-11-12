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

final class NotesController
{
    private readonly PDO $pdo;
    private readonly ActivityLogger $activity;

    public function __construct(App $app)
    {
        $this->pdo = $app->pdo();
        $this->activity = new ActivityLogger($this->pdo);
    }

    /** @param array<string,string> $p */
    public function list(Request $request, array $p): Response
    {
        $siteId = (int) $p['id'];
        (new SiteService($this->pdo))->requireRow($siteId);
        $args = [$siteId];
        $where = 'site_id = ?';
        $q = trim((string) ($request->query['q'] ?? ''));
        if ($q !== '') {
            $like = '%' . str_replace(['\\', '%', '_'], ['\\\\', '\\%', '\\_'], $q) . '%';
            $where .= " AND (title LIKE ? ESCAPE '\\' OR body_md LIKE ? ESCAPE '\\')";
            array_push($args, $like, $like);
        }
        $stmt = $this->pdo->prepare("SELECT id, site_id, title, pinned, created_at, updated_at, substr(body_md, 1, 140) AS excerpt FROM notes WHERE $where ORDER BY pinned DESC, updated_at DESC, id DESC");
        $stmt->execute($args);
        return Response::json(['items' => array_map(fn ($r) => $this->shape($r), $stmt->fetchAll())]);
    }

    /** @param array<string,string> $p */
    public function get(Request $request, array $p): Response
    {
        return Response::json($this->shape($this->row((int) $p['id']), true));
    }

    /** @param array<string,string> $p */
    public function create(Request $request, array $p): Response
    {
        $siteId = (int) $p['id'];
        (new SiteService($this->pdo))->requireRow($siteId);
        $b = $request->json();
        $title = $this->title($b['title'] ?? null);
        $body = $this->body($b['bodyMd'] ?? '');
        $this->pdo->prepare('INSERT INTO notes (site_id, title, body_md, pinned) VALUES (?, ?, ?, ?)')->execute([$siteId, $title, $body, !empty($b['pinned']) ? 1 : 0]);
        $id = (int) $this->pdo->lastInsertId();
        $this->activity->log($siteId, 'note.created', "Note added: $title", ['noteId' => $id]);
        return Response::json($this->shape($this->row($id), true), 201);
    }

    /** @param array<string,string> $p */
    public function patch(Request $request, array $p): Response
    {
        $id = (int) $p['id'];
        $row = $this->row($id);
        $b = $request->json();
        $set = [];
        $args = [];
        $content = array_key_exists('title', $b) || array_key_exists('bodyMd', $b);
        if ($content) {
            // Optimistic concurrency: the editor sends the updatedAt it last saw (docs/PRD.md §8.3).
            if (($b['updatedAt'] ?? null) !== $row['updated_at']) {
                throw new HttpException(409, 'conflict', 'This note was changed somewhere else. Reload it before saving.', [], ['current' => $this->shape($row, true)]);
            }
            if (array_key_exists('title', $b)) {
                $set[] = 'title = ?';
                $args[] = $this->title($b['title']);
            }
            if (array_key_exists('bodyMd', $b)) {
                $set[] = 'body_md = ?';
                $args[] = $this->body($b['bodyMd']);
            }
        }
        if (array_key_exists('pinned', $b)) {
            $set[] = 'pinned = ?';
            $args[] = $b['pinned'] ? 1 : 0;
        }
        if ($set) {
            // A pin change alone should not look like an edit, so updated_at only moves with content.
            $stamp = $content ? ", updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now')" : '';
            $this->pdo->prepare('UPDATE notes SET ' . implode(', ', $set) . "$stamp WHERE id = ?")->execute([...$args, $id]);
        }
        if ($content) {
            $this->logEdit((int) $row['site_id'], $id, (string) ($b['title'] ?? $row['title']));
        }
        return Response::json($this->shape($this->row($id), true));
    }

    /** @param array<string,string> $p */
    public function delete(Request $request, array $p): Response
    {
        $row = $this->row((int) $p['id']);
        $this->pdo->prepare('DELETE FROM notes WHERE id = ?')->execute([$row['id']]);
        $this->activity->log((int) $row['site_id'], 'note.deleted', "Note deleted: {$row['title']}");
        return Response::noContent();
    }

    /** Autosave fires every few seconds; log an edit once per note per half hour. */
    private function logEdit(int $siteId, int $noteId, string $title): void
    {
        $stmt = $this->pdo->prepare(
            "SELECT 1 FROM activity WHERE site_id = ? AND type = 'note.updated' AND json_extract(data_json, '$.noteId') = ?
               AND created_at > strftime('%Y-%m-%dT%H:%M:%fZ','now','-30 minutes') LIMIT 1"
        );
        $stmt->execute([$siteId, $noteId]);
        if (!$stmt->fetchColumn()) {
            $this->activity->log($siteId, 'note.updated', "Note edited: $title", ['noteId' => $noteId]);
        }
    }

    /** @return array<string,mixed> */
    private function row(int $id): array
    {
        $stmt = $this->pdo->prepare('SELECT * FROM notes WHERE id = ?');
        $stmt->execute([$id]);
        return $stmt->fetch() ?: throw new HttpException(404, 'not_found', 'Note not found.');
    }

    /** @param array<string,mixed> $r @return array<string,mixed> */
    private function shape(array $r, bool $withBody = false): array
    {
        $out = [
            'id' => (int) $r['id'], 'siteId' => (int) $r['site_id'], 'title' => $r['title'], 'pinned' => (bool) $r['pinned'],
            'createdAt' => $r['created_at'], 'updatedAt' => $r['updated_at'],
        ];
        if ($withBody) {
            $out['bodyMd'] = $r['body_md'];
        } else {
            $out['excerpt'] = $r['excerpt'] ?? '';
        }
        return $out;
    }

    private function title(mixed $value): string
    {
        $title = is_string($value) ? trim($value) : '';
        if ($title === '' || mb_strlen($title) > 200) {
            throw new HttpException(422, 'validation_failed', 'A note needs a title of up to 200 characters.', ['title' => '1 to 200 characters.']);
        }
        return $title;
    }

    private function body(mixed $value): string
    {
        $body = is_string($value) ? $value : '';
        if (mb_strlen($body) > 200000) {
            throw new HttpException(413, 'too_large', 'Notes are limited to 200,000 characters.', ['bodyMd' => 'Too long.']);
        }
        return $body;
    }
}
