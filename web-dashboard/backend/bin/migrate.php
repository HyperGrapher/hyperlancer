<?php
declare(strict_types=1);

// Usage: php backend/bin/migrate.php [--status] [--import-legacy]
if (PHP_SAPI !== 'cli') {
    exit(1);
}
require dirname(__DIR__) . '/bootstrap.php';

$app = App\App::boot();
$migrator = $app->migrator();

if (in_array('--status', $argv, true)) {
    echo json_encode($migrator->status(), JSON_PRETTY_PRINT), PHP_EOL;
    exit(0);
}

if (in_array('--import-legacy', $argv, true)) {
    // Moves the old notes-demo database out of the build-wiped locations into data/legacy/.
    $root = $app->config->string('project_root');
    foreach (['backend/data', 'dist/backend/data'] as $rel) {
        foreach (glob("$root/$rel/app.sqlite*") ?: [] as $file) {
            $dir = $app->paths->ensureDir('legacy', str_replace('/', '-', $rel));
            rename($file, $dir . DIRECTORY_SEPARATOR . basename($file));
            echo "moved $file\n";
        }
    }
    exit(0);
}

$done = $migrator->migrate();
echo $done ? 'Applied: ' . implode(', ', $done) : 'Up to date.', PHP_EOL;
