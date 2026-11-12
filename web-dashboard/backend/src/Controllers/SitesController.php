<?php
declare(strict_types=1);

namespace App\Controllers;

use App\App;
use App\Domain\ActivityLogger;
use App\Domain\SiteQuery;
use App\Domain\SiteService;
use App\Http\HttpException;
use App\Http\Request;
use App\Http\Response;
use PDO;

final class SitesController
{
    private readonly PDO $pdo;
    private readonly SiteService $sites;
    private readonly ActivityLogger $activity;

    public function __construct(private readonly App $app)
    {
        $this->pdo = $app->pdo();
        $this->sites = new SiteService($this->pdo);
        $this->activity = new ActivityLogger($this->pdo);
    }

    public function list(Request $request): Response
    {
        return Response::json((new SiteQuery($this->pdo))->search($request->query));
    }

    public function checkUrl(Request $request): Response
    {
        [, $n] = $this->sites->normalizeUrl($this->app->urlNormalizer(), $request->query['url'] ?? null);
        return Response::json([
            'normalizedUrl' => $n['url'],
            'siteKey' => $n['siteKey'],
            'registrableDomain' => $n['registrableDomain'],
            'blockedReason' => $this->sites->guard()->blockedReason($n['host']),
            'existing' => $this->sites->findByKey($n['siteKey']),
            'related' => $this->sites->related($n['registrableDomain'], $n['siteKey']),
        ]);
    }

    public function create(Request $request): Response
    {
        $body = $request->json();
        [, $n] = $this->sites->normalizeUrl($this->app->urlNormalizer(), $body['url'] ?? null);
        $allowPrivate = (bool) ($body['allowPrivateNet'] ?? false);
        if ($this->sites->guard()->blockedReason($n['host'], $allowPrivate) !== null) {
            throw new HttpException(422, 'blocked', 'That address is on a private network.', ['url' => 'Tick "Allow local/LAN target" to add a private address.']);
        }
        $existing = $this->sites->findByKey($n['siteKey']);
        if ($existing && !($body['allowDuplicate'] ?? false)) {
            throw new HttpException(409, 'duplicate', "{$n['siteKey']} is already in your list.", [], ['existing' => $existing]);
        }
        $status = $this->sites->validateStatus($body['status'] ?? 'none');
        $category = $this->sites->validateCategory($body['categoryId'] ?? null);
        $tags = $this->sites->validateTags($body['tags'] ?? []);
        $name = isset($body['name']) && trim((string) $body['name']) !== '' ? trim((string) $body['name']) : null;
        if ($name !== null && mb_strlen($name) > 120) {
            throw new HttpException(422, 'validation_failed', 'Name too long.', ['name' => 'At most 120 characters.']);
        }

        $key = $existing ? $this->sites->uniqueKey($n['siteKey']) : $n['siteKey'];
        $this->pdo->beginTransaction();
        try {
            $this->pdo->prepare(
                'INSERT INTO sites (site_key, registrable_domain, url, name, status, category_id, allow_private_net) VALUES (?, ?, ?, ?, ?, ?, ?)'
            )->execute([$key, $n['registrableDomain'], $n['url'], $name, $status, $category, $allowPrivate ? 1 : 0]);
            $id = (int) $this->pdo->lastInsertId();
            $this->sites->setTags($id, $tags);
            $this->activity->log($id, 'site.created', "Added $key");
            $this->pdo->commit();
        } catch (\Throwable $e) {
            $this->pdo->rollBack();
            throw $e;
        }

        $job = ($body['fetch'] ?? true) !== false ? $this->enqueueProfile($id) : null;
        return Response::json(['site' => $this->sites->detail($id), 'job' => $job], 201);
    }

    /** @param array<string,string> $p */
    public function get(Request $request, array $p): Response
    {
        return Response::json($this->sites->detail((int) $p['id']));
    }

    /** @param array<string,string> $p */
    public function patch(Request $request, array $p): Response
    {
        $id = (int) $p['id'];
        $row = $this->sites->requireRow($id);
        $body = $request->json();
        $set = [];
        $args = [];

        if (array_key_exists('url', $body)) {
            [, $n] = $this->sites->normalizeUrl($this->app->urlNormalizer(), $body['url']);
            $allowPrivate = array_key_exists('allowPrivateNet', $body) ? (bool) $body['allowPrivateNet'] : (bool) $row['allow_private_net'];
            if ($this->sites->guard()->blockedReason($n['host'], $allowPrivate) !== null) {
                throw new HttpException(422, 'blocked', 'That address is on a private network.', ['url' => 'Enable "Allow local/LAN target" first.']);
            }
            $other = $this->sites->findByKey($n['siteKey']);
            if ($other && $other['id'] !== $id) {
                throw new HttpException(409, 'duplicate', "{$n['siteKey']} is already in your list.", [], ['existing' => $other]);
            }
            if ($n['siteKey'] !== $row['site_key']) {
                array_push($set, 'site_key = ?', 'registrable_domain = ?');
                array_push($args, $n['siteKey'], $n['registrableDomain']);
            }
            array_push($set, 'url = ?');
            $args[] = $n['url'];
        }
        if (array_key_exists('allowPrivateNet', $body)) {
            $set[] = 'allow_private_net = ?';
            $args[] = $body['allowPrivateNet'] ? 1 : 0;
        }
        if (array_key_exists('name', $body)) {
            $name = $body['name'] === null ? null : trim((string) $body['name']);
            if ($name !== null && mb_strlen($name) > 120) {
                throw new HttpException(422, 'validation_failed', 'Name too long.', ['name' => 'At most 120 characters.']);
            }
            $set[] = 'name = ?';
            $args[] = $name === '' ? null : $name;
        }
        $status = null;
        if (array_key_exists('status', $body)) {
            $status = $this->sites->validateStatus($body['status']);
            $set[] = 'status = ?';
            $args[] = $status;
        }
        $category = false;
        if (array_key_exists('categoryId', $body)) {
            $category = $this->sites->validateCategory($body['categoryId']);
            $set[] = 'category_id = ?';
            $args[] = $category;
        }
        if (array_key_exists('previewUrl', $body)) {
            $set[] = 'preview_url = ?';
            $args[] = $this->sites->validatePreviewUrl($body['previewUrl']);
        }
        if (array_key_exists('deadline', $body)) {
            $set[] = 'deadline = ?';
            $args[] = $this->sites->validateDeadline($body['deadline']);
        }
        $tags = array_key_exists('tags', $body) ? $this->sites->validateTags($body['tags']) : null;

        $this->pdo->beginTransaction();
        try {
            if ($set) {
                $this->pdo->prepare('UPDATE sites SET ' . implode(', ', $set) . ", updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id = ?")
                    ->execute([...$args, $id]);
            }
            if ($status !== null && $status !== $row['status']) {
                $this->activity->log($id, 'status.changed', "Status: {$row['status']} → $status", ['from' => $row['status'], 'to' => $status]);
            }
            if ($category !== false && $category !== ($row['category_id'] === null ? null : (int) $row['category_id'])) {
                $to = $category === null ? 'none' : (string) $this->pdo->query('SELECT name FROM categories WHERE id = ' . (int) $category)->fetchColumn();
                $this->activity->log($id, 'category.changed', 'Category: ' . ($row['category_name'] ?? 'none') . " → $to");
            }
            if ($tags !== null) {
                [$added, $removed] = $this->sites->setTags($id, $tags);
                if ($added || $removed) {
                    $parts = array_merge(array_map(static fn ($t) => "+$t", $added), array_map(static fn ($t) => "−$t", $removed));
                    $this->activity->log($id, 'tags.changed', 'Tags: ' . implode(' ', $parts), ['added' => $added, 'removed' => $removed]);
                    $this->pdo->prepare("UPDATE sites SET updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id = ?")->execute([$id]);
                }
            }
            $this->pdo->commit();
        } catch (\Throwable $e) {
            $this->pdo->rollBack();
            throw $e;
        }
        return Response::json($this->sites->detail($id));
    }

    /** @param array<string,string> $p */
    public function delete(Request $request, array $p): Response
    {
        $id = (int) $p['id'];
        $row = $this->sites->requireRow($id);
        if (($request->query['mode'] ?? 'archive') === 'delete') {
            if (($request->query['confirm'] ?? '') !== $row['site_key']) {
                throw new HttpException(422, 'confirmation_required', 'Type the site key to confirm deletion.', ['confirm' => 'Must equal ' . $row['site_key']]);
            }
            $this->app->crawls()->deleteForSite($id);
            $this->pdo->prepare('DELETE FROM sites WHERE id = ?')->execute([$id]);
            return Response::noContent();
        }
        $this->pdo->prepare("UPDATE sites SET archived_at = strftime('%Y-%m-%dT%H:%M:%fZ','now'), updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id = ?")->execute([$id]);
        $this->activity->log($id, 'site.archived', 'Archived');
        return Response::noContent();
    }

    /** @param array<string,string> $p */
    public function restore(Request $request, array $p): Response
    {
        $id = (int) $p['id'];
        $this->sites->requireRow($id);
        $this->pdo->prepare("UPDATE sites SET archived_at = NULL, updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id = ?")->execute([$id]);
        $this->activity->log($id, 'site.restored', 'Restored from archive');
        return Response::json($this->sites->detail($id));
    }

    public function bulk(Request $request): Response
    {
        $body = $request->json();
        $ids = array_values(array_unique(array_map('intval', (array) ($body['ids'] ?? []))));
        if (!$ids || count($ids) > 500) {
            throw new HttpException(422, 'validation_failed', 'Select between 1 and 500 sites.', ['ids' => 'Select between 1 and 500 sites.']);
        }
        $action = (string) ($body['action'] ?? '');
        $value = $body['value'] ?? null;
        $jobs = [];
        $updated = 0;
        $this->pdo->beginTransaction();
        try {
            foreach ($ids as $id) {
                $row = $this->pdo->prepare('SELECT s.*, c.name AS category_name FROM sites s LEFT JOIN categories c ON c.id = s.category_id WHERE s.id = ?');
                $row->execute([$id]);
                $site = $row->fetch();
                if (!$site) {
                    continue;
                }
                switch ($action) {
                    case 'setStatus':
                        $status = $this->sites->validateStatus($value);
                        if ($status !== $site['status']) {
                            $this->touch($id, 'status = ?', [$status]);
                            $this->activity->log($id, 'status.changed', "Status: {$site['status']} → $status", ['from' => $site['status'], 'to' => $status]);
                        }
                        break;
                    case 'setCategory':
                        $category = $this->sites->validateCategory($value);
                        $this->touch($id, 'category_id = ?', [$category]);
                        $this->activity->log($id, 'category.changed', 'Category changed');
                        break;
                    case 'addTag':
                    case 'removeTag':
                        $name = $this->sites->validateTags([$value])[0] ?? throw new HttpException(422, 'validation_failed', 'Choose a tag.', ['value' => 'Choose a tag.']);
                        $current = array_column($this->pdo->query('SELECT t.name FROM site_tags st JOIN tags t ON t.id = st.tag_id WHERE st.site_id = ' . $id)->fetchAll(), 'name');
                        $next = $action === 'addTag'
                            ? array_merge($current, [$name])
                            : array_values(array_filter($current, static fn ($t) => mb_strtolower($t) !== mb_strtolower($name)));
                        [$added, $removed] = $this->sites->setTags($id, $next);
                        if ($added || $removed) {
                            $this->activity->log($id, 'tags.changed', 'Tags: ' . implode(' ', array_merge(array_map(static fn ($t) => "+$t", $added), array_map(static fn ($t) => "−$t", $removed))));
                            $this->touch($id, 'updated_at = updated_at', []);
                        }
                        break;
                    case 'archive':
                        $this->touch($id, "archived_at = strftime('%Y-%m-%dT%H:%M:%fZ','now')", []);
                        $this->activity->log($id, 'site.archived', 'Archived');
                        break;
                    case 'restore':
                        $this->touch($id, 'archived_at = NULL', []);
                        $this->activity->log($id, 'site.restored', 'Restored from archive');
                        break;
                    case 'refetch':
                        break; // queued after the transaction
                    default:
                        throw new HttpException(422, 'validation_failed', 'Unknown bulk action.', ['action' => 'Unknown action.']);
                }
                $updated++;
            }
            $this->pdo->commit();
        } catch (\Throwable $e) {
            $this->pdo->rollBack();
            throw $e;
        }
        if ($action === 'refetch') {
            foreach ($ids as $id) {
                if ($this->pdo->query('SELECT 1 FROM sites WHERE id = ' . $id)->fetchColumn() && !$this->activeProfileJob($id)) {
                    $jobs[] = $this->enqueueProfile($id);
                }
            }
        }
        return Response::json(['updated' => $updated, 'jobs' => $jobs]);
    }

    /**
     * Serves the site's favicon from this server. The browser cannot always load it itself: the site may have an
     * invalid certificate, redirect http to https, or refuse hotlinking. Fetched once and cached in data/favicons.
     *
     * @param array<string,string> $p
     */
    public function favicon(Request $request, array $p): Response
    {
        $row = $this->sites->requireRow((int) $p['id']);
        $url = (string) ($row['favicon_url'] ?? '');
        if ($url === '') {
            throw new HttpException(404, 'not_found', 'No favicon.');
        }
        $dir = $this->app->paths->ensureDir('favicons');
        $cache = $dir . DIRECTORY_SEPARATOR . $row['id'] . '-' . substr(sha1($url), 0, 12) . '.img';
        $image = is_file($cache) && filemtime($cache) > time() - 7 * 86400 ? file_get_contents($cache) : false;
        if ($image === false) {
            $image = $this->downloadImage($url, (bool) $row['allow_private_net']);
            if ($image === null) {
                throw new HttpException(404, 'not_found', 'The favicon could not be fetched.');
            }
            foreach (glob($dir . DIRECTORY_SEPARATOR . $row['id'] . '-*.img') ?: [] as $old) {
                @unlink($old);
            }
            file_put_contents($cache, $image);
        }
        $type = self::sniffImage($image) ?? 'application/octet-stream';
        return new Response(200, $image, [
            'Content-Type' => $type,
            'Cache-Control' => 'private, max-age=86400',
            // Even if someone opens the URL directly, an SVG from a stranger's site must not run scripts here.
            'Content-Security-Policy' => "default-src 'none'; sandbox",
        ]);
    }

    /** Image type from the file's first bytes (the server's Content-Type is not trusted). */
    public static function sniffImage(string $bytes): ?string
    {
        return match (true) {
            str_starts_with($bytes, "\x89PNG") => 'image/png',
            str_starts_with($bytes, "\xFF\xD8") => 'image/jpeg',
            str_starts_with($bytes, 'GIF8') => 'image/gif',
            str_starts_with($bytes, "\x00\x00\x01\x00") => 'image/x-icon',
            str_starts_with($bytes, 'RIFF') && substr($bytes, 8, 4) === 'WEBP' => 'image/webp',
            (bool) preg_match('/^\s*(<\?xml[^>]*>\s*)?(<!--.*?-->\s*)*<svg[\s>]/is', substr($bytes, 0, 1024)) => 'image/svg+xml',
            default => null,
        };
    }

    private function downloadImage(string $url, bool $allowPrivate): ?string
    {
        for ($hop = 0; $hop < 4; $hop++) {
            $parts = parse_url($url);
            if (!$parts || !in_array($parts['scheme'] ?? '', ['http', 'https'], true) || empty($parts['host'])) {
                return null;
            }
            if ($this->sites->guard()->blockedReason($parts['host'], $allowPrivate) !== null) {
                return null; // every hop is checked, so a redirect cannot lead into the local network
            }
            $context = stream_context_create([
                'http' => ['method' => 'GET', 'timeout' => 6, 'follow_location' => 0, 'ignore_errors' => true,
                    'header' => "User-Agent: FreelanceDashboard/1 (favicon)\r\nAccept: image/*,*/*;q=0.5\r\n"],
                'ssl' => ['verify_peer' => false, 'verify_peer_name' => false], // we only display the image
            ]);
            $body = @file_get_contents($url, false, $context, 0, 300001);
            $status = 0;
            $location = null;
            foreach ($http_response_header ?? [] as $line) {
                if (preg_match('#^HTTP/\S+ (\d+)#', $line, $m)) {
                    $status = (int) $m[1];
                    $location = null;
                } elseif (stripos($line, 'Location:') === 0) {
                    $location = trim(substr($line, 9));
                }
            }
            if ($status >= 300 && $status < 400 && $location) {
                $url = self::resolveUrl($url, $location);
                continue;
            }
            if ($status !== 200 || $body === false || strlen($body) > 300000) {
                return null;
            }
            return self::sniffImage($body) !== null ? $body : null;
        }
        return null;
    }

    private static function resolveUrl(string $base, string $location): string
    {
        if (preg_match('#^https?://#i', $location)) {
            return $location;
        }
        $parts = parse_url($base);
        $origin = ($parts['scheme'] ?? 'http') . '://' . ($parts['host'] ?? '') . (isset($parts['port']) ? ':' . $parts['port'] : '');
        if (str_starts_with($location, '//')) {
            return ($parts['scheme'] ?? 'http') . ':' . $location;
        }
        if (str_starts_with($location, '/')) {
            return $origin . $location;
        }
        return $origin . rtrim(dirname($parts['path'] ?? '/'), '/') . '/' . $location;
    }

    /** @param array<string,string> $p */
    public function fetch(Request $request, array $p): Response
    {
        $id = (int) $p['id'];
        $this->sites->requireRow($id);
        if ($this->activeProfileJob($id)) {
            throw new HttpException(409, 'conflict', 'A profile fetch is already running for this site.');
        }
        return Response::json(['job' => $this->enqueueProfile($id)], 202);
    }

    /** @param list<mixed> $args */
    private function touch(int $id, string $assignment, array $args): void
    {
        $this->pdo->prepare("UPDATE sites SET $assignment, updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id = ?")->execute([...$args, $id]);
    }

    private function activeProfileJob(int $siteId): bool
    {
        return (bool) $this->pdo->query("SELECT 1 FROM jobs WHERE site_id = $siteId AND type = 'profile' AND status IN ('queued','running')")->fetchColumn();
    }

    /** @return array<string,mixed> */
    private function enqueueProfile(int $siteId): array
    {
        $site = $this->pdo->query('SELECT url, allow_private_net FROM sites WHERE id = ' . $siteId)->fetch();
        $job = $this->app->jobs()->enqueue('profile', $siteId, ['url' => $site['url'], 'allowPrivate' => (bool) $site['allow_private_net']]);
        $this->app->launcher()->ensureWorker();
        return $job;
    }
}
