<?php
declare(strict_types=1);

// Stand-in for the Python tools: behaviour is chosen by params.mode in the job spec.
$spec = json_decode((string) file_get_contents($argv[array_search('--spec', $argv, true) + 1]), true);
$mode = $spec['params']['mode'] ?? 'success';
$emit = static function (array $event): void {
    echo json_encode($event), "\n";
    fflush(STDOUT);
};

switch ($mode) {
    case 'success':
    case 'partial':
        $emit(['type' => 'log', 'level' => 'info', 'message' => 'hello from fake job']);
        for ($i = 1; $i <= 3; $i++) {
            $emit(['type' => 'progress', 'current' => $i, 'total' => 3, 'label' => "step $i"]);
        }
        $emit(['type' => 'stats', 'pages' => 7, 'bytes' => 1234]);
        echo "this line is not JSON\n";
        $emit(['type' => 'result', 'path' => 'x.json', 'status' => $mode === 'partial' ? 'partial' : 'ok']);
        exit($mode === 'partial' ? 1 : 0);
    case 'crash':
        fwrite(STDERR, "Traceback: boom\n");
        exit(3);
    case 'sleep':
        for ($i = 0; $i < 60; $i++) {
            if (is_file((string) $spec['cancelFile'])) {
                exit(130);
            }
            sleep(1);
        }
        exit(0);
}
exit(2);
