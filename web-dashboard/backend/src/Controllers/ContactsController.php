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

/** Contacts and typed links of a site (docs/PRD.md §5.13). */
final class ContactsController
{
    private const CHANNELS = ['email', 'phone', 'whatsapp', 'telegram', 'other'];
    private const LINK_KINDS = ['production', 'staging', 'repo', 'hosting', 'registrar', 'dns', 'analytics', 'design', 'other'];

    private readonly PDO $pdo;
    private readonly ActivityLogger $activity;

    public function __construct(App $app)
    {
        $this->pdo = $app->pdo();
        $this->activity = new ActivityLogger($this->pdo);
    }

    // ---- contacts --------------------------------------------------------------------------------------------

    /** @param array<string,string> $p */
    public function contacts(Request $request, array $p): Response
    {
        (new SiteService($this->pdo))->requireRow((int) $p['id']);
        $stmt = $this->pdo->prepare('SELECT * FROM contacts WHERE site_id = ? ORDER BY id');
        $stmt->execute([(int) $p['id']]);
        return Response::json(['items' => array_map([$this, 'contact'], $stmt->fetchAll())]);
    }

    /** @param array<string,string> $p */
    public function createContact(Request $request, array $p): Response
    {
        $siteId = (int) $p['id'];
        (new SiteService($this->pdo))->requireRow($siteId);
        $v = $this->contactFields($request->json(), true);
        $this->pdo->prepare('INSERT INTO contacts (site_id, name, role, email, phone, preferred_channel, notes) VALUES (?, ?, ?, ?, ?, ?, ?)')
            ->execute([$siteId, $v['name'], $v['role'], $v['email'], $v['phone'], $v['preferred_channel'], $v['notes']]);
        $id = (int) $this->pdo->lastInsertId();
        $this->activity->log($siteId, 'contact.created', "Contact added: {$v['name']}");
        return Response::json($this->contact($this->contactRow($id)), 201);
    }

    /** @param array<string,string> $p */
    public function updateContact(Request $request, array $p): Response
    {
        $row = $this->contactRow((int) $p['id']);
        $v = $this->contactFields($request->json(), false);
        if ($v) {
            $set = implode(', ', array_map(static fn ($c) => "$c = ?", array_keys($v)));
            $this->pdo->prepare("UPDATE contacts SET $set, updated_at = strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id = ?")->execute([...array_values($v), $row['id']]);
            $this->activity->log((int) $row['site_id'], 'contact.updated', 'Contact updated: ' . ($v['name'] ?? $row['name']));
        }
        return Response::json($this->contact($this->contactRow((int) $row['id'])));
    }

    /** @param array<string,string> $p */
    public function deleteContact(Request $request, array $p): Response
    {
        $row = $this->contactRow((int) $p['id']);
        $this->pdo->prepare('DELETE FROM contacts WHERE id = ?')->execute([$row['id']]);
        $this->activity->log((int) $row['site_id'], 'contact.deleted', "Contact removed: {$row['name']}");
        return Response::noContent();
    }

    /** @return array<string,mixed> */
    private function contactRow(int $id): array
    {
        $stmt = $this->pdo->prepare('SELECT * FROM contacts WHERE id = ?');
        $stmt->execute([$id]);
        return $stmt->fetch() ?: throw new HttpException(404, 'not_found', 'Contact not found.');
    }

    /** @param array<string,mixed> $r @return array<string,mixed> */
    private function contact(array $r): array
    {
        return ['id' => (int) $r['id'], 'siteId' => (int) $r['site_id'], 'name' => $r['name'], 'role' => $r['role'], 'email' => $r['email'],
            'phone' => $r['phone'], 'preferredChannel' => $r['preferred_channel'], 'notes' => $r['notes']];
    }

    /** @param array<string,mixed> $b @return array<string,mixed> column => value, only the keys present (all of them when $create) */
    private function contactFields(array $b, bool $create): array
    {
        $out = [];
        $text = function (string $key, string $column, int $max, bool $required = false) use ($b, &$out, $create): void {
            if (!$create && !array_key_exists($key, $b)) {
                return;
            }
            $value = isset($b[$key]) ? trim((string) $b[$key]) : '';
            if ($required && $value === '') {
                throw new HttpException(422, 'validation_failed', 'A contact needs a name.', [$key => 'Required.']);
            }
            if (mb_strlen($value) > $max) {
                throw new HttpException(422, 'validation_failed', "$key is too long.", [$key => "At most $max characters."]);
            }
            $out[$column] = $value === '' ? null : $value;
        };
        $text('name', 'name', 120, true);
        $text('role', 'role', 120);
        $text('notes', 'notes', 2000);
        $text('email', 'email', 200);
        $text('phone', 'phone', 40);
        if (isset($out['email']) && !filter_var($out['email'], FILTER_VALIDATE_EMAIL)) {
            throw new HttpException(422, 'validation_failed', 'Invalid email address.', ['email' => 'Enter a valid email address.']);
        }
        if (isset($out['phone']) && !preg_match('/^\+?[0-9 ()\-.\/]{5,40}$/', $out['phone'])) {
            throw new HttpException(422, 'validation_failed', 'Invalid phone number.', ['phone' => 'Use digits, spaces and + ( ) - only.']);
        }
        if ($create || array_key_exists('preferredChannel', $b)) {
            $channel = $b['preferredChannel'] ?? null;
            if ($channel !== null && $channel !== '' && !in_array($channel, self::CHANNELS, true)) {
                throw new HttpException(422, 'validation_failed', 'Invalid channel.', ['preferredChannel' => 'Use one of: ' . implode(', ', self::CHANNELS)]);
            }
            $out['preferred_channel'] = $channel === '' ? null : $channel;
        }
        return $out;
    }

    // ---- links -----------------------------------------------------------------------------------------------

    /** @param array<string,string> $p */
    public function links(Request $request, array $p): Response
    {
        (new SiteService($this->pdo))->requireRow((int) $p['id']);
        $stmt = $this->pdo->prepare('SELECT * FROM site_links WHERE site_id = ? ORDER BY sort_order, id');
        $stmt->execute([(int) $p['id']]);
        return Response::json(['items' => array_map([$this, 'link'], $stmt->fetchAll())]);
    }

    /** @param array<string,string> $p */
    public function createLink(Request $request, array $p): Response
    {
        $siteId = (int) $p['id'];
        (new SiteService($this->pdo))->requireRow($siteId);
        $b = $request->json();
        $kind = $this->kind($b['kind'] ?? 'other');
        $url = $this->url($b['url'] ?? null);
        $label = isset($b['label']) && trim((string) $b['label']) !== '' ? mb_substr(trim((string) $b['label']), 0, 80) : null;
        $order = (int) $this->pdo->query("SELECT COALESCE(max(sort_order), 0) + 1 FROM site_links WHERE site_id = $siteId")->fetchColumn();
        $this->pdo->prepare('INSERT INTO site_links (site_id, kind, label, url, sort_order) VALUES (?, ?, ?, ?, ?)')->execute([$siteId, $kind, $label, $url, $order]);
        $id = (int) $this->pdo->lastInsertId();
        $this->activity->log($siteId, 'link.created', "Link added ($kind): " . ($label ?? $url));
        return Response::json($this->link($this->linkRow($id)), 201);
    }

    /** @param array<string,string> $p */
    public function updateLink(Request $request, array $p): Response
    {
        $row = $this->linkRow((int) $p['id']);
        $b = $request->json();
        $kind = array_key_exists('kind', $b) ? $this->kind($b['kind']) : $row['kind'];
        $url = array_key_exists('url', $b) ? $this->url($b['url']) : $row['url'];
        $label = array_key_exists('label', $b) ? (trim((string) $b['label']) === '' ? null : mb_substr(trim((string) $b['label']), 0, 80)) : $row['label'];
        $this->pdo->prepare('UPDATE site_links SET kind = ?, label = ?, url = ? WHERE id = ?')->execute([$kind, $label, $url, $row['id']]);
        return Response::json($this->link($this->linkRow((int) $row['id'])));
    }

    /** @param array<string,string> $p */
    public function deleteLink(Request $request, array $p): Response
    {
        $row = $this->linkRow((int) $p['id']);
        $this->pdo->prepare('DELETE FROM site_links WHERE id = ?')->execute([$row['id']]);
        $this->activity->log((int) $row['site_id'], 'link.deleted', 'Link removed: ' . ($row['label'] ?? $row['url']));
        return Response::noContent();
    }

    /** @return array<string,mixed> */
    private function linkRow(int $id): array
    {
        $stmt = $this->pdo->prepare('SELECT * FROM site_links WHERE id = ?');
        $stmt->execute([$id]);
        return $stmt->fetch() ?: throw new HttpException(404, 'not_found', 'Link not found.');
    }

    /** @param array<string,mixed> $r @return array<string,mixed> */
    private function link(array $r): array
    {
        return ['id' => (int) $r['id'], 'siteId' => (int) $r['site_id'], 'kind' => $r['kind'], 'label' => $r['label'], 'url' => $r['url']];
    }

    private function kind(mixed $kind): string
    {
        if (!in_array($kind, self::LINK_KINDS, true)) {
            throw new HttpException(422, 'validation_failed', 'Invalid link type.', ['kind' => 'Use one of: ' . implode(', ', self::LINK_KINDS)]);
        }
        return $kind;
    }

    private function url(mixed $value): string
    {
        $url = trim((string) $value);
        if ($url !== '' && !preg_match('#^[a-z][a-z0-9+.-]*://#i', $url)) {
            $url = 'https://' . $url;
        }
        if (!preg_match('#^https?://#i', $url) || !filter_var($url, FILTER_VALIDATE_URL) || strlen($url) > 500) {
            throw new HttpException(422, 'validation_failed', 'Invalid link URL.', ['url' => 'Enter a valid http or https URL.']);
        }
        return $url;
    }
}
