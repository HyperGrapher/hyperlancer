<?php
declare(strict_types=1);

namespace App\Domain;

use PDO;

final class ActivityLogger
{
    public function __construct(private readonly PDO $pdo)
    {
    }

    /** @param array<string,mixed>|null $data */
    public function log(?int $siteId, string $type, string $message, ?array $data = null): void
    {
        $this->pdo->prepare('INSERT INTO activity (site_id, type, message, data_json) VALUES (?, ?, ?, ?)')
            ->execute([$siteId, $type, $message, $data === null ? null : json_encode($data, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES)]);
    }
}
