<?php
declare(strict_types=1);

namespace App\Domain;

use PDO;

/**
 * Turns the list filters into SQL. Only allow-listed filters and sort columns reach the query text; every value is
 * bound (docs/PRD.md §11).
 */
final class SiteQuery
{
    public const STATUSES = ['none', 'design', 'waiting', 'done', 'canceled'];
    private const SORTS = [
        'updated' => 's.updated_at', 'fetched' => 's.last_fetched_at', 'domain' => 's.site_key', 'deadline' => 's.deadline',
        'status' => 's.status', 'cms' => 's.cms', 'created' => 's.created_at',
    ];
    private const NULLS_LAST = ['fetched', 'deadline', 'cms'];

    public function __construct(private readonly PDO $pdo)
    {
    }

    /** @return list<string> */
    private static function csv(mixed $value): array
    {
        if (!is_string($value) || $value === '') {
            return [];
        }
        return array_values(array_unique(array_filter(array_map('trim', explode(',', $value)), static fn ($v) => $v !== '')));
    }

    private static function like(string $value): string
    {
        return '%' . str_replace(['\\', '%', '_'], ['\\\\', '\\%', '\\_'], $value) . '%';
    }

    /**
     * @param array<string,mixed> $q
     * @return array{0:string,1:list<mixed>} WHERE clause and parameters; $skip leaves one facet's own filter out
     */
    private function where(array $q, ?string $skip = null): array
    {
        $sql = [];
        $args = [];

        $archived = ($q['archived'] ?? '0') === '1';
        $sql[] = $archived ? 's.archived_at IS NOT NULL' : 's.archived_at IS NULL';

        if (is_string($q['q'] ?? null) && trim($q['q']) !== '') {
            $like = self::like(trim($q['q']));
            $sql[] = "(s.site_key LIKE ? ESCAPE '\\' OR s.name LIKE ? ESCAPE '\\' OR s.title LIKE ? ESCAPE '\\'
                       OR EXISTS (SELECT 1 FROM notes n WHERE n.site_id = s.id AND n.title LIKE ? ESCAPE '\\')
                       OR EXISTS (SELECT 1 FROM contacts ct WHERE ct.site_id = s.id AND ct.name LIKE ? ESCAPE '\\'))";
            array_push($args, $like, $like, $like, $like, $like);
        }

        $statuses = array_values(array_intersect(self::csv($q['status'] ?? null), self::STATUSES));
        if ($statuses && $skip !== 'status') {
            $sql[] = 's.status IN (' . $this->marks($statuses) . ')';
            array_push($args, ...$statuses);
        }

        $categories = self::csv($q['category'] ?? null);
        if ($categories && $skip !== 'category') {
            $named = array_values(array_filter($categories, static fn ($c) => $c !== 'none'));
            $parts = [];
            if ($named) {
                $parts[] = 's.category_id IN (SELECT id FROM categories WHERE name COLLATE NOCASE IN (' . $this->marks($named) . '))';
                array_push($args, ...$named);
            }
            if (in_array('none', $categories, true)) {
                $parts[] = 's.category_id IS NULL';
            }
            $sql[] = '(' . implode(' OR ', $parts) . ')';
        }

        $tags = self::csv($q['tag'] ?? null);
        if ($tags && $skip !== 'tag') {
            $inner = 'SELECT st.site_id FROM site_tags st JOIN tags t ON t.id = st.tag_id WHERE t.name COLLATE NOCASE IN (' . $this->marks($tags) . ')';
            if (($q['tagMode'] ?? 'any') === 'all') {
                $sql[] = "s.id IN ($inner GROUP BY st.site_id HAVING count(DISTINCT t.id) = CAST(? AS INTEGER))";
                array_push($args, ...$tags);
                $args[] = count($tags);
            } else {
                $sql[] = "s.id IN ($inner)";
                array_push($args, ...$tags);
            }
        }

        $cms = self::csv($q['cms'] ?? null);
        if ($cms && $skip !== 'cms') {
            $named = array_values(array_filter($cms, static fn ($c) => $c !== 'Unknown'));
            $parts = [];
            if ($named) {
                $parts[] = 's.cms COLLATE NOCASE IN (' . $this->marks($named) . ')';
                array_push($args, ...$named);
            }
            if (in_array('Unknown', $cms, true)) {
                $parts[] = 's.cms IS NULL';
            }
            $sql[] = '(' . implode(' OR ', $parts) . ')';
        }

        if (is_string($q['tech'] ?? null) && trim($q['tech']) !== '') {
            $sql[] = "EXISTS (SELECT 1 FROM snapshot_technologies x WHERE x.snapshot_id = s.latest_snapshot_id AND x.name LIKE ? ESCAPE '\\')";
            $args[] = self::like(trim($q['tech']));
        }

        $outcomes = self::csv($q['outcome'] ?? null);
        if ($outcomes && $skip !== 'outcome') {
            $named = array_values(array_intersect($outcomes, ['ok', 'partial', 'failed']));
            $parts = [];
            if ($named) {
                $parts[] = 's.last_fetch_outcome IN (' . $this->marks($named) . ')';
                array_push($args, ...$named);
            }
            if (in_array('never', $outcomes, true)) {
                $parts[] = 's.last_fetch_outcome IS NULL';
            }
            if ($parts) {
                $sql[] = '(' . implode(' OR ', $parts) . ')';
            }
        }

        foreach (['fetched' => 's.last_fetched_at', 'deadline' => 's.deadline'] as $param => $column) {
            if (is_string($q[$param] ?? null) && preg_match('/^(\d{4}-\d{2}-\d{2})?\.\.(\d{4}-\d{2}-\d{2})?$/', $q[$param], $m)) {
                if (($m[1] ?? '') !== '') {
                    $sql[] = "substr($column, 1, 10) >= ?";
                    $args[] = $m[1];
                }
                if (($m[2] ?? '') !== '') {
                    $sql[] = "substr($column, 1, 10) <= ?";
                    $args[] = $m[2];
                }
            }
        }

        if (($q['hasOpenTodos'] ?? null) === '1') {
            $sql[] = 'EXISTS (SELECT 1 FROM todos td WHERE td.site_id = s.id AND td.done = 0)';
        }
        return [implode(' AND ', $sql), $args];
    }

    /** @param list<mixed> $values */
    private function marks(array $values): string
    {
        return implode(',', array_fill(0, count($values), '?'));
    }

    /**
     * @param array<string,mixed> $q
     * @return array{items:list<array<string,mixed>>,total:int,page:int,perPage:int,facets:array<string,mixed>}
     */
    public function search(array $q): array
    {
        [$where, $args] = $this->where($q);
        $count = $this->pdo->prepare("SELECT count(*) FROM sites s WHERE $where");
        $count->execute($args);
        $total = (int) $count->fetchColumn();

        $page = max(1, (int) ($q['page'] ?? 1));
        $perPage = min(200, max(1, (int) ($q['perPage'] ?? 50)));
        $order = $this->order((string) ($q['sort'] ?? '-updated'));

        $stmt = $this->pdo->prepare(
            "SELECT s.*, c.name AS category_name, c.color AS category_color,
                    (SELECT count(*) FROM todos td WHERE td.site_id = s.id AND td.done = 0) AS open_todos
             FROM sites s LEFT JOIN categories c ON c.id = s.category_id
             WHERE $where ORDER BY $order LIMIT $perPage OFFSET " . ($page - 1) * $perPage
        );
        $stmt->execute($args);
        $rows = $stmt->fetchAll();

        return [
            'items' => (new SiteService($this->pdo))->listItems($rows),
            'total' => $total,
            'page' => $page,
            'perPage' => $perPage,
            // Empty facets must encode as {} rather than [].
            'facets' => array_map(static fn (array $f) => $f ?: new \stdClass(), $this->facets($q)),
        ];
    }

    private function order(string $sort): string
    {
        $parts = [];
        foreach (array_slice(self::csv($sort), 0, 2) as $token) {
            $desc = str_starts_with($token, '-');
            $key = ltrim($token, '-');
            if (!isset(self::SORTS[$key])) {
                continue;
            }
            $column = self::SORTS[$key];
            if (in_array($key, self::NULLS_LAST, true)) {
                $parts[] = "($column IS NULL) ASC";
            }
            $parts[] = $column . ($key === 'domain' || $key === 'cms' ? ' COLLATE NOCASE' : '') . ($desc ? ' DESC' : ' ASC');
        }
        $parts[] = 's.id DESC';
        return implode(', ', $parts);
    }

    /** Counts for each filter option under the other active filters. @param array<string,mixed> $q @return array<string,array<string,int>> */
    private function facets(array $q): array
    {
        $run = function (string $facet, string $select, string $join = '') use ($q): array {
            [$where, $args] = $this->where($q, $facet);
            $stmt = $this->pdo->prepare("SELECT $select AS k, count(DISTINCT s.id) AS n FROM sites s $join WHERE $where GROUP BY k");
            $stmt->execute($args);
            $out = [];
            foreach ($stmt->fetchAll() as $row) {
                $out[(string) $row['k']] = (int) $row['n'];
            }
            arsort($out);
            return $out;
        };
        return [
            'status' => $run('status', 's.status'),
            'cms' => $run('cms', "COALESCE(s.cms, 'Unknown')"),
            'category' => $run('category', "COALESCE(c.name, 'none')", 'LEFT JOIN categories c ON c.id = s.category_id'),
            'tag' => $run('tag', 't.name', 'JOIN site_tags st ON st.site_id = s.id JOIN tags t ON t.id = st.tag_id'),
            'outcome' => $run('outcome', "COALESCE(s.last_fetch_outcome, 'never')"),
        ];
    }
}
