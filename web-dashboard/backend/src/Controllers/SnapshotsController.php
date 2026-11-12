<?php
declare(strict_types=1);

namespace App\Controllers;

use App\App;
use App\Domain\ActivityLogger;
use App\Domain\ProfileNormalizer;
use App\Domain\SiteService;
use App\Domain\SnapshotDiff;
use App\Http\HttpException;
use App\Http\Request;
use App\Http\Response;
use PDO;

final class SnapshotsController
{
    private readonly PDO $pdo;
    private readonly SiteService $sites;

    public function __construct(private readonly App $app)
    {
        $this->pdo = $app->pdo();
        $this->sites = new SiteService($this->pdo);
    }

    /** @param array<string,string> $p */
    public function list(Request $request, array $p): Response
    {
        $siteId = (int) $p['id'];
        $this->sites->requireRow($siteId);
        $page = max(1, (int) ($request->query['page'] ?? 1));
        $perPage = 30;
        $total = (int) $this->pdo->query("SELECT count(*) FROM profile_snapshots WHERE site_id = $siteId")->fetchColumn();
        $ids = $this->pdo->query("SELECT id FROM profile_snapshots WHERE site_id = $siteId ORDER BY fetched_at DESC, id DESC LIMIT $perPage OFFSET " . ($page - 1) * $perPage)->fetchAll(PDO::FETCH_COLUMN);
        return Response::json([
            'items' => array_map(fn ($id) => $this->sites->snapshotMeta((int) $id), $ids),
            'total' => $total, 'page' => $page, 'perPage' => $perPage,
        ]);
    }

    /** @param array<string,string> $p */
    public function get(Request $request, array $p): Response
    {
        $row = $this->row((int) $p['id']);
        // The raw report is the source of truth: normalising it again picks up summary fields added since the
        // snapshot was stored, so old snapshots never lack a key the UI expects.
        $rawReport = json_decode($row['raw_json'], true);
        $summary = is_array($rawReport) ? (new ProfileNormalizer())->normalize($rawReport) : json_decode($row['summary_json'], true);
        $out = $this->sites->snapshotMeta((int) $row['id']) + ['summary' => $summary];
        if (($request->query['raw'] ?? '') === '1') {
            $out['raw'] = $rawReport;
        }
        return Response::json($out);
    }

    /** @param array<string,string> $p */
    public function diff(Request $request, array $p): Response
    {
        $b = $this->row((int) $p['id']);
        $a = null;
        if (isset($request->query['against'])) {
            $a = $this->row((int) $request->query['against']);
            if ((int) $a['site_id'] !== (int) $b['site_id']) {
                throw new HttpException(422, 'validation_failed', 'Snapshots belong to different sites.');
            }
            if ((int) $a['id'] > (int) $b['id']) {
                [$a, $b] = [$b, $a]; // always older to newer
            }
        } else {
            $stmt = $this->pdo->prepare("SELECT * FROM profile_snapshots WHERE site_id = ? AND id < ? AND outcome != 'failed' ORDER BY id DESC LIMIT 1");
            $stmt->execute([$b['site_id'], $b['id']]);
            $a = $stmt->fetch() ?: null;
        }
        $meta = fn (?array $r) => $r ? $this->sites->snapshotMeta((int) $r['id']) : null;
        if ($a === null) {
            return Response::json(['a' => null, 'b' => $meta($b), 'groups' => [], 'unchangedSections' => [], 'count' => 0]);
        }
        $result = (new SnapshotDiff())->diff(json_decode($a['summary_json'], true) ?: [], json_decode($b['summary_json'], true) ?: []);
        return Response::json(['a' => $meta($a), 'b' => $meta($b)] + $result);
    }

    /** @param array<string,string> $p */
    public function delete(Request $request, array $p): Response
    {
        $row = $this->row((int) $p['id']);
        $siteId = (int) $row['site_id'];
        $this->pdo->beginTransaction();
        try {
            $this->pdo->prepare('DELETE FROM profile_snapshots WHERE id = ?')->execute([$row['id']]);
            // Repoint the site at the newest good snapshot (or any, or none) and refresh its facts from it.
            $next = $this->pdo->query("SELECT id, summary_json FROM profile_snapshots WHERE site_id = $siteId ORDER BY (outcome = 'failed') ASC, id DESC LIMIT 1")->fetch();
            $this->pdo->prepare('UPDATE sites SET latest_snapshot_id = ? WHERE id = ?')->execute([$next ? $next['id'] : null, $siteId]);
            (new ActivityLogger($this->pdo))->log($siteId, 'snapshot.deleted', 'Deleted a profile snapshot');
            $this->pdo->commit();
        } catch (\Throwable $e) {
            $this->pdo->rollBack();
            throw $e;
        }
        return Response::noContent();
    }

    /** @return array<string,mixed> */
    private function row(int $id): array
    {
        $stmt = $this->pdo->prepare('SELECT * FROM profile_snapshots WHERE id = ?');
        $stmt->execute([$id]);
        return $stmt->fetch() ?: throw new HttpException(404, 'not_found', 'Snapshot not found.');
    }
}
