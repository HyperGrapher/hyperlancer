<?php
declare(strict_types=1);

namespace App\Infra;

use PDO;

final class Backup
{
    public function __construct(private readonly PDO $pdo, private readonly Paths $paths)
    {
    }

    /** @return array{file:string,bytes:int,createdAt:string} */
    public function create(string $label = 'manual'): array
    {
        $dir = $this->paths->ensureDir('backups');
        $label = preg_replace('/[^a-z0-9-]+/i', '-', $label);
        $name = 'app-' . gmdate('Y-m-d\THis') . '-' . $label . '.sqlite';
        $target = $dir . DIRECTORY_SEPARATOR . $name;
        $this->pdo->exec('VACUUM INTO ' . $this->pdo->quote($target));
        if ($label !== 'manual') {
            $this->prune($dir);
        }
        return ['file' => $name, 'bytes' => (int) filesize($target), 'createdAt' => gmdate('c')];
    }

    /** Keep the newest 20 automatic backups. */
    private function prune(string $dir): void
    {
        $auto = array_filter(glob($dir . DIRECTORY_SEPARATOR . 'app-*.sqlite') ?: [], static fn ($f) => !str_ends_with($f, '-manual.sqlite'));
        rsort($auto);
        foreach (array_slice($auto, 20) as $old) {
            @unlink($old);
        }
    }
}
