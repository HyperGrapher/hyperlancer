<?php
declare(strict_types=1);

namespace App\Infra;

use InvalidArgumentException;

/** Every filesystem path under the data dir is built here (docs/PRD.md §10.3). */
final class Paths
{
    public function __construct(private readonly string $dataDir)
    {
    }

    public function data(string ...$segments): string
    {
        $path = $this->dataDir;
        foreach ($segments as $segment) {
            if ($segment === '' || str_contains($segment, "\0") || preg_match('#(^|[\\\\/])\.\.([\\\\/]|$)#', $segment)) {
                throw new InvalidArgumentException('Invalid path segment.');
            }
            $path .= DIRECTORY_SEPARATOR . trim(str_replace(['\\', '/'], DIRECTORY_SEPARATOR, $segment), DIRECTORY_SEPARATOR);
        }
        return $path;
    }

    public function ensureDir(string ...$segments): string
    {
        $dir = $this->data(...$segments);
        if (!is_dir($dir) && !mkdir($dir, 0775, true) && !is_dir($dir)) {
            throw new \RuntimeException("Cannot create directory: $dir");
        }
        return $dir;
    }

    /** DB-stored POSIX relative path to an absolute path. */
    public function fromRelative(string $relative): string
    {
        return $this->data(...explode('/', $relative));
    }

    public function dataDir(): string
    {
        return $this->dataDir;
    }
}
