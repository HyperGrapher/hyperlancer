<?php
declare(strict_types=1);

namespace App\Controllers;

use App\App;
use App\Http\HttpException;
use App\Http\Request;
use App\Http\Response;

final class JobsController
{
    public function __construct(private readonly App $app)
    {
    }

    public function list(Request $request): Response
    {
        $this->app->launcher()->ensureWorker();
        $q = $request->query;
        $site = isset($q['site']) && ctype_digit((string) $q['site']) ? (int) $q['site'] : null;
        return Response::json($this->app->jobs()->list(
            in_array($q['status'] ?? 'all', ['active', 'finished', 'all'], true) ? (string) ($q['status'] ?? 'all') : 'all',
            isset($q['type']) ? (string) $q['type'] : null,
            $site,
            (int) ($q['page'] ?? 1),
            (int) ($q['perPage'] ?? 50),
        ));
    }

    /** @param array<string,string> $p */
    public function get(Request $request, array $p): Response
    {
        $this->app->launcher()->ensureWorker();
        return Response::json($this->find((int) $p['id']));
    }

    /** @param array<string,string> $p */
    public function log(Request $request, array $p): Response
    {
        $row = $this->app->jobs()->row((int) $p['id']) ?? throw new HttpException(404, 'not_found', 'Job not found.');
        $file = $this->app->paths->data('jobs', (string) $row['id'], 'job.log');
        $offset = max(0, (int) ($request->query['offset'] ?? 0));
        $limit = min(65536, max(1, (int) ($request->query['limit'] ?? 65536)));
        $text = '';
        $size = is_file($file) ? (int) filesize($file) : 0;
        if ($size > $offset) {
            $fh = fopen($file, 'rb');
            if ($fh !== false) {
                fseek($fh, $offset);
                $text = (string) fread($fh, min($limit, $size - $offset));
                fclose($fh);
            }
        }
        $next = $offset + strlen($text);
        return Response::json([
            'text' => mb_convert_encoding($text, 'UTF-8', 'UTF-8'),
            'nextOffset' => $next,
            'eof' => $next >= $size && !in_array($row['status'], ['queued', 'running'], true),
        ]);
    }

    /** @param array<string,string> $p */
    public function cancel(Request $request, array $p): Response
    {
        $this->find((int) $p['id']);
        return Response::json($this->app->jobs()->requestCancel((int) $p['id']), 202);
    }

    /** @param array<string,string> $p */
    public function retry(Request $request, array $p): Response
    {
        $row = $this->app->jobs()->row((int) $p['id']) ?? throw new HttpException(404, 'not_found', 'Job not found.');
        if (in_array($row['status'], ['queued', 'running'], true)) {
            throw new HttpException(409, 'conflict', 'Job is still active.');
        }
        $job = $this->app->jobs()->enqueue((string) $row['type'], $row['site_id'] === null ? null : (int) $row['site_id'], json_decode((string) $row['params_json'], true) ?: []);
        $this->app->launcher()->ensureWorker();
        return Response::json(['job' => $job], 202);
    }

    /** @return array<string,mixed> */
    private function find(int $id): array
    {
        return $this->app->jobs()->find($id) ?? throw new HttpException(404, 'not_found', 'Job not found.');
    }
}
