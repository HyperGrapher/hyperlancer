<?php
declare(strict_types=1);

// API front controller: routes /api/* (docs/PRD.md §8.1).
require __DIR__ . '/bootstrap.php';

App\App::handle(App\Http\Request::fromGlobals())->send();
