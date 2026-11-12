<?php
declare(strict_types=1);

// Front controller, copied to dist/index.php (docs/DECISIONS.md D9).
// /api/* goes to the PHP backend; any other path serves the SPA so deep links like /sites/12 work.
$path = (string) parse_url((string) ($_SERVER['REQUEST_URI'] ?? '/'), PHP_URL_PATH);

if (str_starts_with($path, '/api/') || str_starts_with($path, '/index.php/api/')) {
    require __DIR__ . '/backend/bootstrap.php';
    App\App::handle(App\Http\Request::fromGlobals())->send();
    return;
}

// Real static files are served by the web server; a missing asset must stay a 404, not the SPA shell.
if (pathinfo($path, PATHINFO_EXTENSION) !== '') {
    http_response_code(404);
    header('Content-Type: text/plain; charset=utf-8');
    echo 'Not found';
    return;
}

header('Content-Type: text/html; charset=utf-8');
header('Cache-Control: no-cache');
// Everything is bundled, so scripts come only from this origin. Images may be remote (Markdown notes).
header("Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data: http: https:; font-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'self'; form-action 'self'; frame-ancestors 'none'");
header('X-Content-Type-Options: nosniff');
header('X-Frame-Options: DENY');
header('Referrer-Policy: no-referrer');
readfile(__DIR__ . '/index.html');
