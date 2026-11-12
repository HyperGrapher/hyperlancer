<?php
declare(strict_types=1);

// Router script for `php -S 127.0.0.1:8787 backend/dev-router.php` (APP_ENV=development).
$path = parse_url((string) $_SERVER['REQUEST_URI'], PHP_URL_PATH);
if (str_starts_with((string) $path, '/api/')) {
    require __DIR__ . '/index.php';
    return true;
}
http_response_code(404);
header('Content-Type: application/json');
echo '{"error":{"code":"not_found","message":"The dev API server only serves /api/*."}}';
return true;
