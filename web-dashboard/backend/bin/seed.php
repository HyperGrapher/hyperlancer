<?php
declare(strict_types=1);

// Usage: php backend/bin/seed.php --demo   (idempotent; imports the four example profiles as v1 snapshots)
if (PHP_SAPI !== 'cli') {
    exit(1);
}
require dirname(__DIR__) . '/bootstrap.php';

if (!in_array('--demo', $argv, true)) {
    fwrite(STDERR, "Nothing to do. Pass --demo to load demo data.\n");
    exit(2);
}

$app = App\App::boot();
$app->migrator()->migrate();
$pdo = $app->pdo();
$service = new App\Domain\SiteService($pdo);
$normalizer = $app->urlNormalizer();
$ingestor = $app->ingestor();

$categories = ['Food & agriculture' => 'emerald', 'Industry' => 'amber', 'Energy' => 'sky', 'Retail' => 'rose'];
foreach ($categories as $name => $color) {
    $pdo->prepare('INSERT OR IGNORE INTO categories (name, color, sort_order) VALUES (?, ?, (SELECT COALESCE(max(sort_order), 0) + 1 FROM categories))')->execute([$name, $color]);
}
$categoryId = static fn (string $name) => (int) $pdo->query('SELECT id FROM categories WHERE name = ' . $pdo->quote($name))->fetchColumn();

$demo = [
    'forazeytin.com.tr' => ['Food & agriculture', 'design', ['seo', 'rebuild']],
    'oleoolivia.com' => ['Food & agriculture', 'waiting', ['wix']],
    'renama.com.tr' => ['Industry', 'waiting', ['wp']],
    'resoltenerji.com' => ['Energy', 'none', ['tls']],
];
$dir = dirname(__DIR__, 2) . '/scripts/examples';
$added = 0;
foreach ($demo as $host => [$category, $status, $tags]) {
    $file = "$dir/$host.json";
    $existing = $service->findByKey($host);
    if ($existing || !is_file($file)) {
        continue;
    }
    $n = $normalizer->normalize($host);
    $pdo->prepare('INSERT INTO sites (site_key, registrable_domain, url, status, category_id) VALUES (?, ?, ?, ?, ?)')
        ->execute([$n['siteKey'], $n['registrableDomain'], $n['url'], $status, $categoryId($category)]);
    $id = (int) $pdo->lastInsertId();
    $service->setTags($id, $tags);
    $ingestor->ingest($id, null, json_decode((string) file_get_contents($file), true, 512, JSON_THROW_ON_ERROR));
    $added++;
}
$pdo->exec("INSERT OR IGNORE INTO saved_views (name, query, sort_order) VALUES ('Waiting on client', 'status=waiting', 1), ('WordPress sites', 'cms=WordPress', 2)");
echo "Demo data ready ($added site(s) added).\n";
