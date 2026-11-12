<?php
declare(strict_types=1);

namespace App\Controllers;

use App\App;
use App\Http\HttpException;
use App\Http\Request;
use App\Http\Response;
use PDO;
use PDOException;

/** Categories, tags and saved views: small named lists with uniqueness rules. */
final class TaxonomyController
{
    private readonly PDO $pdo;

    public function __construct(App $app)
    {
        $this->pdo = $app->pdo();
    }

    // ---- categories ------------------------------------------------------------------------------------------

    public function categories(): Response
    {
        return Response::json(['items' => array_map(static fn ($r) => [
            'id' => (int) $r['id'], 'name' => $r['name'], 'color' => $r['color'], 'sortOrder' => (int) $r['sort_order'], 'siteCount' => (int) $r['n'],
        ], $this->pdo->query(
            'SELECT c.*, (SELECT count(*) FROM sites s WHERE s.category_id = c.id AND s.archived_at IS NULL) AS n FROM categories c ORDER BY c.sort_order, c.name COLLATE NOCASE'
        )->fetchAll())]);
    }

    public function createCategory(Request $request): Response
    {
        $b = $request->json();
        $name = $this->name($b['name'] ?? null, 60);
        $this->guardUnique(fn () => $this->pdo->prepare('INSERT INTO categories (name, color, sort_order) VALUES (?, ?, (SELECT COALESCE(max(sort_order), 0) + 1 FROM categories))')
            ->execute([$name, $this->color($b['color'] ?? null)]), 'A category with that name already exists.');
        return Response::json($this->categoryById((int) $this->pdo->lastInsertId()), 201);
    }

    /** @param array<string,string> $p */
    public function updateCategory(Request $request, array $p): Response
    {
        $id = (int) $p['id'];
        $this->categoryById($id);
        $b = $request->json();
        if (array_key_exists('name', $b)) {
            $this->guardUnique(fn () => $this->pdo->prepare('UPDATE categories SET name = ? WHERE id = ?')->execute([$this->name($b['name'], 60), $id]), 'A category with that name already exists.');
        }
        if (array_key_exists('color', $b)) {
            $this->pdo->prepare('UPDATE categories SET color = ? WHERE id = ?')->execute([$this->color($b['color']), $id]);
        }
        if (isset($b['sortOrder'])) {
            $this->pdo->prepare('UPDATE categories SET sort_order = ? WHERE id = ?')->execute([(int) $b['sortOrder'], $id]);
        }
        return Response::json($this->categoryById($id));
    }

    /** @param array<string,string> $p */
    public function deleteCategory(Request $request, array $p): Response
    {
        $this->categoryById((int) $p['id']);
        $this->pdo->prepare('DELETE FROM categories WHERE id = ?')->execute([(int) $p['id']]); // sites.category_id becomes NULL
        return Response::noContent();
    }

    /** @return array<string,mixed> */
    private function categoryById(int $id): array
    {
        $stmt = $this->pdo->prepare('SELECT * FROM categories WHERE id = ?');
        $stmt->execute([$id]);
        $r = $stmt->fetch() ?: throw new HttpException(404, 'not_found', 'Category not found.');
        return ['id' => (int) $r['id'], 'name' => $r['name'], 'color' => $r['color'], 'sortOrder' => (int) $r['sort_order']];
    }

    // ---- tags ------------------------------------------------------------------------------------------------

    public function tags(): Response
    {
        return Response::json(['items' => array_map(static fn ($r) => [
            'id' => (int) $r['id'], 'name' => $r['name'], 'color' => $r['color'], 'siteCount' => (int) $r['n'],
        ], $this->pdo->query(
            'SELECT t.*, (SELECT count(*) FROM site_tags st JOIN sites s ON s.id = st.site_id WHERE st.tag_id = t.id AND s.archived_at IS NULL) AS n FROM tags t ORDER BY t.name COLLATE NOCASE'
        )->fetchAll())]);
    }

    public function createTag(Request $request): Response
    {
        $b = $request->json();
        $name = $this->name($b['name'] ?? null, 40);
        $this->guardUnique(fn () => $this->pdo->prepare('INSERT INTO tags (name, color) VALUES (?, ?)')->execute([$name, $this->color($b['color'] ?? null)]), 'A tag with that name already exists.');
        return Response::json($this->tagById((int) $this->pdo->lastInsertId()), 201);
    }

    /** @param array<string,string> $p */
    public function updateTag(Request $request, array $p): Response
    {
        $id = (int) $p['id'];
        $this->tagById($id);
        $b = $request->json();
        if (array_key_exists('name', $b)) {
            $this->guardUnique(fn () => $this->pdo->prepare('UPDATE tags SET name = ? WHERE id = ?')->execute([$this->name($b['name'], 40), $id]), 'A tag with that name already exists. Use merge instead.');
        }
        if (array_key_exists('color', $b)) {
            $this->pdo->prepare('UPDATE tags SET color = ? WHERE id = ?')->execute([$this->color($b['color']), $id]);
        }
        return Response::json($this->tagById($id));
    }

    /** @param array<string,string> $p */
    public function deleteTag(Request $request, array $p): Response
    {
        $this->tagById((int) $p['id']);
        $this->pdo->prepare('DELETE FROM tags WHERE id = ?')->execute([(int) $p['id']]);
        return Response::noContent();
    }

    /** @param array<string,string> $p */
    public function mergeTag(Request $request, array $p): Response
    {
        $from = (int) $p['id'];
        $into = (int) ($request->json()['intoId'] ?? 0);
        $this->tagById($from);
        $target = $this->tagById($into);
        if ($from === $into) {
            throw new HttpException(422, 'validation_failed', 'Choose a different tag to merge into.', ['intoId' => 'Choose a different tag.']);
        }
        $this->pdo->beginTransaction();
        try {
            $this->pdo->prepare('INSERT OR IGNORE INTO site_tags (site_id, tag_id) SELECT site_id, ? FROM site_tags WHERE tag_id = ?')->execute([$into, $from]);
            $this->pdo->prepare('DELETE FROM tags WHERE id = ?')->execute([$from]); // cascades its site_tags rows
            $this->pdo->commit();
        } catch (\Throwable $e) {
            $this->pdo->rollBack();
            throw $e;
        }
        return Response::json($target);
    }

    /** @return array<string,mixed> */
    private function tagById(int $id): array
    {
        $stmt = $this->pdo->prepare('SELECT * FROM tags WHERE id = ?');
        $stmt->execute([$id]);
        $r = $stmt->fetch() ?: throw new HttpException(404, 'not_found', 'Tag not found.');
        return ['id' => (int) $r['id'], 'name' => $r['name'], 'color' => $r['color']];
    }

    // ---- saved views -----------------------------------------------------------------------------------------

    public function views(): Response
    {
        return Response::json(['items' => array_map(static fn ($r) => [
            'id' => (int) $r['id'], 'name' => $r['name'], 'query' => $r['query'], 'sortOrder' => (int) $r['sort_order'],
        ], $this->pdo->query('SELECT * FROM saved_views ORDER BY sort_order, id')->fetchAll())]);
    }

    public function createView(Request $request): Response
    {
        $b = $request->json();
        $name = $this->name($b['name'] ?? null, 60);
        $query = ltrim(trim((string) ($b['query'] ?? '')), '?');
        $this->guardUnique(fn () => $this->pdo->prepare('INSERT INTO saved_views (name, query, sort_order) VALUES (?, ?, (SELECT COALESCE(max(sort_order), 0) + 1 FROM saved_views))')->execute([$name, $query]), 'A view with that name already exists.');
        return Response::json($this->viewById((int) $this->pdo->lastInsertId()), 201);
    }

    /** @param array<string,string> $p */
    public function updateView(Request $request, array $p): Response
    {
        $id = (int) $p['id'];
        $this->viewById($id);
        $b = $request->json();
        if (array_key_exists('name', $b)) {
            $this->guardUnique(fn () => $this->pdo->prepare('UPDATE saved_views SET name = ? WHERE id = ?')->execute([$this->name($b['name'], 60), $id]), 'A view with that name already exists.');
        }
        if (array_key_exists('query', $b)) {
            $this->pdo->prepare('UPDATE saved_views SET query = ? WHERE id = ?')->execute([ltrim(trim((string) $b['query']), '?'), $id]);
        }
        return Response::json($this->viewById($id));
    }

    /** @param array<string,string> $p */
    public function deleteView(Request $request, array $p): Response
    {
        $this->viewById((int) $p['id']);
        $this->pdo->prepare('DELETE FROM saved_views WHERE id = ?')->execute([(int) $p['id']]);
        return Response::noContent();
    }

    /** @return array<string,mixed> */
    private function viewById(int $id): array
    {
        $stmt = $this->pdo->prepare('SELECT * FROM saved_views WHERE id = ?');
        $stmt->execute([$id]);
        $r = $stmt->fetch() ?: throw new HttpException(404, 'not_found', 'View not found.');
        return ['id' => (int) $r['id'], 'name' => $r['name'], 'query' => $r['query'], 'sortOrder' => (int) $r['sort_order']];
    }

    // ---- helpers ---------------------------------------------------------------------------------------------

    private function name(mixed $value, int $max): string
    {
        $name = is_string($value) ? trim($value) : '';
        if ($name === '' || mb_strlen($name) > $max) {
            throw new HttpException(422, 'validation_failed', "Enter a name of 1 to $max characters.", ['name' => "1 to $max characters."]);
        }
        return $name;
    }

    private function color(mixed $value): ?string
    {
        if ($value === null || $value === '') {
            return null;
        }
        if (!is_string($value) || !preg_match('/^[a-z0-9#-]{1,20}$/i', $value)) {
            throw new HttpException(422, 'validation_failed', 'Invalid colour.', ['color' => 'Use a colour name such as emerald.']);
        }
        return $value;
    }

    private function guardUnique(callable $write, string $message): void
    {
        try {
            $write();
        } catch (PDOException $e) {
            if (str_contains($e->getMessage(), 'UNIQUE')) {
                throw new HttpException(409, 'conflict', $message, ['name' => $message]);
            }
            throw $e;
        }
    }
}
