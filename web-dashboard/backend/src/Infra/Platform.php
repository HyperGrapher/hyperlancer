<?php
declare(strict_types=1);

namespace App\Infra;

final class Platform
{
    public static function isWindows(): bool
    {
        return PHP_OS_FAMILY === 'Windows';
    }

    public static function nullDevice(): string
    {
        return self::isWindows() ? 'NUL' : '/dev/null';
    }

    public static function isCli(): bool
    {
        return PHP_SAPI === 'cli';
    }
}
