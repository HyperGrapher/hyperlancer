<?php
declare(strict_types=1);

// Spawn-spike child: sleeps while emitting NDJSON progress events, stops when the cancel file appears.
if (PHP_SAPI !== 'cli') {
    exit(1);
}
$specFile = $argv[array_search('--spec', $argv, true) + 1] ?? '';
$spec = json_decode((string) @file_get_contents($specFile), true);
if (!is_array($spec)) {
    fwrite(STDERR, "selfcheck: missing --spec\n");
    exit(2);
}
$seconds = (int) ($spec['params']['seconds'] ?? 180);
$emit = static function (array $event): void {
    echo json_encode($event), "\n";
    fflush(STDOUT);
};
$emit(['type' => 'log', 'level' => 'info', 'message' => "selfcheck started, pid " . getmypid() . ", $seconds s"]);
for ($i = 1; $i <= $seconds; $i++) {
    sleep(1);
    if (is_file((string) $spec['cancelFile'])) {
        $emit(['type' => 'log', 'level' => 'warn', 'message' => 'cancel file seen, stopping']);
        exit(130);
    }
    $emit(['type' => 'progress', 'current' => $i, 'total' => $seconds, 'label' => 'sleeping']);
}
$emit(['type' => 'result', 'ok' => true, 'pid' => getmypid()]);
exit(0);
