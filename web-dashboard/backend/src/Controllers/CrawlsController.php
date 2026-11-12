<?php
declare(strict_types=1);

namespace App\Controllers;

use App\App;
use App\Domain\CrawlReader;
use App\Domain\CrawlStore;
use App\Domain\SiteService;
use App\Http\HttpException;
use App\Http\Request;
use App\Http\Response;
use App\Infra\Platform;

final class CrawlsController
{
    private readonly CrawlStore $crawls;
    private readonly SiteService $sites;

    public function __construct(private readonly App $app)
    {
        $this->crawls = $app->crawls();
        $this->sites = new SiteService($app->pdo());
    }

    /** @param array<string,string> $p */
    public function forSite(Request $request, array $p): Response
    {
        $this->sites->requireRow((int) $p['id']);
        $this->app->launcher()->ensureWorker();
        return Response::json(['items' => $this->crawls->forSite((int) $p['id'])]);
    }

    /** @param array<string,string> $p */
    public function create(Request $request, array $p): Response
    {
        $site = $this->sites->requireRow((int) $p['id']);
        $opts = $this->crawls->validateOptions($request->json(), (string) $site['url']);
        $crawl = $this->crawls->create($site, $opts);
        $this->app->launcher()->ensureWorker();
        (new \App\Domain\ActivityLogger($this->app->pdo()))->log((int) $site['id'], 'crawl.started', 'Crawl started', ['crawlId' => $crawl['id']]);
        return Response::json(['crawl' => $crawl, 'job' => $crawl['job']], 202);
    }

    /** @param array<string,string> $p */
    public function get(Request $request, array $p): Response
    {
        $this->app->launcher()->ensureWorker();
        $row = $this->crawls->require((int) $p['id']);
        return Response::json($this->crawls->toApi($row) + ['manifest' => $this->reader($row)->manifest()]);
    }

    /** @param array<string,string> $p */
    public function pages(Request $request, array $p): Response
    {
        return Response::json($this->reader($this->crawls->require((int) $p['id']))->pages((string) ($request->query['q'] ?? '')));
    }

    /** @param array<string,string> $p */
    public function page(Request $request, array $p): Response
    {
        return Response::json($this->reader($this->crawls->require((int) $p['id']))->page($p['pageId']));
    }

    /** @param array<string,string> $p */
    public function assets(Request $request, array $p): Response
    {
        $q = $request->query;
        $kind = in_array($q['kind'] ?? '', ['image', 'file', 'media'], true) ? (string) $q['kind'] : '';
        return Response::json($this->reader($this->crawls->require((int) $p['id']))->assets(
            $kind,
            isset($q['pageId']) && $q['pageId'] !== '' ? (string) $q['pageId'] : null,
            (int) ($q['minWidth'] ?? 0),
            (int) ($q['page'] ?? 1),
            (int) ($q['perPage'] ?? 60),
        ));
    }

    /** @param array<string,string> $p */
    public function raw(Request $request, array $p): Response
    {
        $raw = $this->reader($this->crawls->require((int) $p['id']))->raw((string) ($request->query['path'] ?? ''));
        $download = ($request->query['download'] ?? '') === '1' || !$raw['inline'];
        return Response::inlineFile($raw['path'], $raw['contentType'], $download ? $raw['name'] : null);
    }

    /** @param array<string,string> $p */
    public function buildArchive(Request $request, array $p): Response
    {
        $row = $this->crawls->require((int) $p['id']);
        $api = $this->crawls->toApi($row);
        if (in_array($api['status'], ['queued', 'running', 'canceling'], true)) {
            throw new HttpException(409, 'conflict', 'The crawl is still running.');
        }
        if ($api['archiveReady']) {
            return Response::json(['ready' => true]);
        }
        $existing = $this->app->pdo()->prepare("SELECT id FROM jobs WHERE type = 'archive' AND status IN ('queued','running') AND json_extract(params_json, '$.crawlId') = ? LIMIT 1");
        $existing->execute([(int) $row['id']]);
        $jobId = $existing->fetchColumn();
        $job = $jobId ? $this->app->jobs()->find((int) $jobId) : null;
        if ($job === null) {
            $rel = 'archives/crawl-' . (int) $row['id'] . '.zip';
            $this->app->paths->ensureDir('archives');
            $job = $this->app->jobs()->enqueue('archive', (int) $row['site_id'], [
                'crawlId' => (int) $row['id'], 'sourceDir' => $this->crawls->absDir($row), 'target' => $this->app->paths->fromRelative($rel), 'archive' => $rel,
            ]);
            $this->app->launcher()->ensureWorker();
        }
        return Response::json(['ready' => false, 'job' => $job], 202);
    }

    /** @param array<string,string> $p */
    public function downloadArchive(Request $request, array $p): Response
    {
        $row = $this->crawls->require((int) $p['id']);
        if ($row['archive_path'] === null || !is_file($this->app->paths->fromRelative((string) $row['archive_path']))) {
            throw new HttpException(404, 'not_found', 'No archive has been built yet.');
        }
        $name = preg_replace('/[^A-Za-z0-9._-]+/', '_', basename((string) $row['output_dir'])) ?: 'crawl';
        $site = $this->sites->requireRow((int) $row['site_id']);
        return Response::file($this->app->paths->fromRelative((string) $row['archive_path']), $site['site_key'] . '-' . $name . '.zip', 'application/zip');
    }

    /** @param array<string,string> $p */
    public function reveal(Request $request, array $p): Response
    {
        $dir = $this->crawls->absDir($this->crawls->require((int) $p['id']));
        $real = realpath($dir);
        if ($real === false) {
            throw new HttpException(404, 'not_found', 'The crawl folder is missing.');
        }
        if (Platform::isWindows()) {
            pclose(popen('start "" explorer.exe ' . escapeshellarg($real), 'r'));
        } elseif (PHP_OS_FAMILY === 'Linux' && trim((string) @shell_exec('command -v xdg-open')) !== '') {
            @exec('xdg-open ' . escapeshellarg($real) . ' >/dev/null 2>&1 &');
        } else {
            throw new HttpException(501, 'unsupported', 'Opening a folder is not supported here. Use the copied path instead.');
        }
        return Response::noContent();
    }

    /** @param array<string,string> $p */
    public function delete(Request $request, array $p): Response
    {
        $row = $this->crawls->require((int) $p['id']);
        if (in_array($this->crawls->toApi($row)['status'], ['queued', 'running', 'canceling'], true)) {
            throw new HttpException(409, 'conflict', 'Cancel the crawl before deleting it.');
        }
        $this->crawls->delete($row);
        return Response::noContent();
    }

    /** @param array<string,mixed> $row */
    private function reader(array $row): CrawlReader
    {
        return new CrawlReader($this->crawls->absDir($row));
    }
}
