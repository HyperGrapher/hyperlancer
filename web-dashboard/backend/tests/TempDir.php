<?php
declare(strict_types=1);

namespace App\Tests;

final class TempDir
{
    public static function create(): string
    {
        $dir = sys_get_temp_dir() . DIRECTORY_SEPARATOR . 'fd-test-' . bin2hex(random_bytes(6));
        mkdir($dir, 0775, true);
        return $dir;
    }

    public static function remove(string $dir): void
    {
        if (!is_dir($dir)) {
            return;
        }
        foreach (new \RecursiveIteratorIterator(new \RecursiveDirectoryIterator($dir, \FilesystemIterator::SKIP_DOTS), \RecursiveIteratorIterator::CHILD_FIRST) as $f) {
            $f->isDir() ? @rmdir($f->getPathname()) : @unlink($f->getPathname());
        }
        @rmdir($dir);
    }
}
