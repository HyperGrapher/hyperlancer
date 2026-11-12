<?php
declare(strict_types=1);

// Job worker. Usage: php backend/bin/worker.php [--daemon]
if (PHP_SAPI !== 'cli') {
    http_response_code(403);
    exit('CLI only.');
}
require dirname(__DIR__) . '/bootstrap.php';

set_time_limit(0);
ignore_user_abort(true);

$app = App\App::boot();
$app->migrator()->migrate();
$worker = new App\Jobs\Worker(
    $app->config,
    $app->paths,
    $app->jobs(),
    App\Jobs\JobLauncher::processControl(),
    $app->logger('worker.log'),
    ['onFinish' => [$app, 'onJobFinished']],
);
exit($worker->run(in_array('--daemon', $argv, true)));
