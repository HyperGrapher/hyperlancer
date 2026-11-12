<?php
declare(strict_types=1);

namespace App\Infra;

use PDO;

final class Database
{
    public static function open(Paths $paths): PDO
    {
        $paths->ensureDir();
        $pdo = new PDO('sqlite:' . $paths->data('app.sqlite'), null, null, [
            PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
            PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
            PDO::ATTR_EMULATE_PREPARES => false,
        ]);
        $pdo->exec('PRAGMA busy_timeout=5000');
        $pdo->exec('PRAGMA foreign_keys=ON');
        $pdo->exec('PRAGMA journal_mode=WAL');
        $pdo->exec('PRAGMA synchronous=NORMAL');
        return $pdo;
    }
}
