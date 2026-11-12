<?php
declare(strict_types=1);

namespace App\Controllers;

use App\App;
use App\Domain\SiteService;
use App\Http\Request;
use App\Http\Response;
use PDO;

final class ActivityController
{
    private readonly PDO $pdo;

    public function __construct(App $app)
    {
        $this->pdo = $app->pdo();
    }

    /** @param array<string,string> $p */
    public function forSite(Request $request, array $p): Response
    {
        (new SiteService($this->pdo))->requireRow((int) $p['id']);
        return $this->page($request, 'a.site_id = ?', [(int) $p['id']]);
    }

    public function all(Request $request): Response
    {
        return $this->page($request, '1 = 1', []);
    }

    /** Keyset pagination: pass the previous response's nextBefore as `before`. @param list<mixed> $args */
    private function page(Request $request, string $where, array $args): Response
    {
        $limit = min(100, max(1, (int) ($request->query['limit'] ?? 30)));
        if (isset($request->query['before']) && ctype_digit((string) $request->query['before'])) {
            $where .= ' AND a.id < ?';
            $args[] = (int) $request->query['before'];
        }
        $stmt = $this->pdo->prepare(
            "SELECT a.*, s.site_key FROM activity a LEFT JOIN sites s ON s.id = a.site_id WHERE $where ORDER BY a.id DESC LIMIT " . ($limit + 1)
        );
        $stmt->execute($args);
        $rows = $stmt->fetchAll();
        $more = count($rows) > $limit;
        $rows = array_slice($rows, 0, $limit);
        return Response::json([
            'items' => array_map(static fn ($r) => [
                'id' => (int) $r['id'], 'siteId' => $r['site_id'] === null ? null : (int) $r['site_id'], 'siteKey' => $r['site_key'],
                'type' => $r['type'], 'message' => $r['message'], 'data' => $r['data_json'] ? json_decode($r['data_json'], true) : null,
                'createdAt' => $r['created_at'],
            ], $rows),
            'nextBefore' => $more ? (int) end($rows)['id'] : null,
        ]);
    }
}
