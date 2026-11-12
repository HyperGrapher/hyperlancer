<?php
declare(strict_types=1);

namespace App\Domain;

use App\Http\HttpException;
use App\Security\SafePath;

/** Reads a finished (or running) crawl folder written by scripts/site_crawl. */
final class CrawlReader
{
    private const INLINE_TYPES = [
        'png' => 'image/png', 'jpg' => 'image/jpeg', 'jpeg' => 'image/jpeg', 'gif' => 'image/gif', 'webp' => 'image/webp',
        'avif' => 'image/avif', 'bmp' => 'image/bmp', 'ico' => 'image/x-icon', 'svg' => 'image/svg+xml',
        'pdf' => 'application/pdf', 'md' => 'text/plain; charset=utf-8', 'txt' => 'text/plain; charset=utf-8',
        'json' => 'text/plain; charset=utf-8', 'log' => 'text/plain; charset=utf-8', 'csv' => 'text/plain; charset=utf-8',
    ];
    /** Types the browser may render in place (the CSP sandbox header keeps SVG inert); everything else downloads. */
    private const DISPLAY = ['png', 'jpg', 'jpeg', 'gif', 'webp', 'avif', 'bmp', 'ico', 'svg', 'pdf', 'md', 'txt', 'json', 'log', 'csv'];

    public function __construct(private readonly string $root)
    {
    }

    /** @return array<string,mixed>|null */
    public function json(string $name): ?array
    {
        $file = $this->root . DIRECTORY_SEPARATOR . $name;
        $text = is_file($file) ? @file_get_contents($file) : false;
        $data = $text === false ? null : json_decode($text, true);
        return is_array($data) ? $data : null;
    }

    /** @return array<string,mixed>|null */
    public function manifest(): ?array
    {
        return $this->json('manifest.json');
    }

    /** @return array{items:list<array<string,mixed>>,tree:mixed} */
    public function pages(string $q = ''): array
    {
        $structure = $this->json('structure.json') ?? ['pages' => [], 'tree' => null];
        $q = mb_strtolower(trim($q));
        $items = [];
        foreach ($structure['pages'] ?? [] as $p) {
            if ($q !== '' && !str_contains(mb_strtolower(($p['title'] ?? '') . ' ' . ($p['url'] ?? '')), $q)) {
                continue;
            }
            $items[] = [
                'id' => $p['id'], 'url' => $p['url'], 'file' => $p['file'], 'title' => $p['title'] ?? '', 'depth' => (int) ($p['depth'] ?? 0),
                'wordCount' => (int) ($p['word_count'] ?? 0), 'images' => count($p['images'] ?? []), 'lang' => $p['lang'] ?? null,
            ];
        }
        return ['items' => $items, 'tree' => $structure['tree'] ?? null];
    }

    /** @return array{meta:array<string,mixed>,markdown:string} */
    public function page(string $pageId): array
    {
        $structure = $this->json('structure.json') ?? ['pages' => []];
        $entry = null;
        foreach ($structure['pages'] ?? [] as $p) {
            if ((string) $p['id'] === $pageId) {
                $entry = $p;
                break;
            }
        }
        if ($entry === null) {
            throw new HttpException(404, 'not_found', 'Page not found.');
        }
        $text = (string) @file_get_contents($this->safe((string) $entry['file']));
        if (preg_match('/\A---\R.*?\R---\R?/s', $text, $m)) {
            $text = ltrim(substr($text, strlen($m[0])));
        }
        return ['meta' => $entry, 'markdown' => $text];
    }

    /** @return array{items:list<array<string,mixed>>,total:int,page:int,perPage:int} */
    public function assets(string $kind, ?string $pageId, int $minWidth, int $page, int $perPage): array
    {
        $all = ($this->json('assets.json') ?? [])['assets'] ?? [];
        $items = [];
        foreach ($all as $a) {
            if ($kind !== '' && $a['kind'] !== $kind) {
                continue;
            }
            if ($pageId !== null && !in_array($pageId, $a['used_on'] ?? [], true)) {
                continue;
            }
            if ($minWidth > 0 && (int) ($a['width'] ?? 0) < $minWidth) {
                continue;
            }
            $items[] = [
                'id' => $a['id'], 'kind' => $a['kind'], 'url' => $a['url'], 'file' => $a['file'], 'bytes' => (int) $a['bytes'], 'mime' => $a['mime'],
                'width' => $a['width'] ?? null, 'height' => $a['height'] ?? null, 'altTexts' => $a['alt_texts'] ?? [], 'usedOn' => $a['used_on'] ?? [],
            ];
        }
        $perPage = max(1, min(200, $perPage));
        $page = max(1, $page);
        return ['items' => array_slice($items, ($page - 1) * $perPage, $perPage), 'total' => count($items), 'page' => $page, 'perPage' => $perPage];
    }

    /** @return array{path:string,contentType:string,inline:bool,name:string} */
    public function raw(string $relative): array
    {
        $path = $this->safe($relative);
        $ext = strtolower(pathinfo($path, PATHINFO_EXTENSION));
        $inline = in_array($ext, self::DISPLAY, true);
        return [
            'path' => $path,
            'contentType' => self::INLINE_TYPES[$ext] ?? 'application/octet-stream',
            'inline' => $inline,
            'name' => basename($path),
        ];
    }

    public function safe(string $relative): string
    {
        try {
            return SafePath::resolve($this->root, $relative);
        } catch (\InvalidArgumentException) {
            throw new HttpException(404, 'not_found', 'File not found.');
        }
    }

    /** Deletes a crawl folder; never follows symlinks. */
    public static function removeTree(string $dir): void
    {
        if (!is_dir($dir) || is_link($dir)) {
            return;
        }
        $it = new \RecursiveIteratorIterator(new \RecursiveDirectoryIterator($dir, \FilesystemIterator::SKIP_DOTS), \RecursiveIteratorIterator::CHILD_FIRST);
        foreach ($it as $f) {
            ($f->isDir() && !$f->isLink()) ? @rmdir($f->getPathname()) : @unlink($f->getPathname());
        }
        @rmdir($dir);
    }
}
