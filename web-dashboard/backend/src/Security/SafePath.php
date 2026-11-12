<?php
declare(strict_types=1);

namespace App\Security;

use InvalidArgumentException;

/** Resolves a user-supplied relative path inside a crawl folder; anything that could leave it is refused. */
final class SafePath
{
    /** @throws InvalidArgumentException */
    public static function resolve(string $root, string $relative): string
    {
        if ($relative === '' || str_contains($relative, "\0") || str_contains($relative, '\\')
            || $relative[0] === '/' || preg_match('#^[A-Za-z]:#', $relative) || preg_match('#(^|/)\.\.?(/|$)#', $relative)) {
            throw new InvalidArgumentException('Invalid path.');
        }
        $base = realpath($root);
        if ($base === false) {
            throw new InvalidArgumentException('Folder not found.');
        }
        $full = $base . DIRECTORY_SEPARATOR . str_replace('/', DIRECTORY_SEPARATOR, $relative);
        if (is_link($full)) {
            throw new InvalidArgumentException('Invalid path.');
        }
        $real = realpath($full);
        if ($real === false || !str_starts_with($real, $base . DIRECTORY_SEPARATOR) || !is_file($real)) {
            throw new InvalidArgumentException('File not found.');
        }
        return $real;
    }
}
