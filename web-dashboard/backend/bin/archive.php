<?php
declare(strict_types=1);

// Archive job child: zips one crawl folder. Usage (via the worker): php archive.php --spec spec.json
if (PHP_SAPI !== 'cli') {
    exit(1);
}
$specFile = $argv[array_search('--spec', $argv, true) + 1] ?? '';
$spec = json_decode((string) @file_get_contents($specFile), true);
$source = is_array($spec) ? (string) ($spec['params']['sourceDir'] ?? '') : '';
$target = is_array($spec) ? (string) ($spec['params']['target'] ?? '') : '';
$emit = static function (array $event): void {
    echo json_encode($event), "\n";
    fflush(STDOUT);
};
if ($source === '' || $target === '' || !is_dir($source) || !class_exists('ZipArchive')) {
    fwrite(STDERR, "archive: bad spec or the PHP zip extension is missing\n");
    exit(2);
}
@mkdir(dirname($target), 0775, true);
$tmp = $target . '.part';
@unlink($tmp);
$zip = new ZipArchive();
if ($zip->open($tmp, ZipArchive::CREATE | ZipArchive::OVERWRITE) !== true) {
    fwrite(STDERR, "archive: cannot create $tmp\n");
    exit(3);
}
$base = rtrim(realpath($source) ?: $source, '/\\');
$files = [];
$it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($base, FilesystemIterator::SKIP_DOTS));
foreach ($it as $f) {
    if ($f->isFile() && !$f->isLink()) {
        $files[] = $f->getPathname();
    }
}
sort($files);
$root = basename($source);
$total = count($files);
foreach ($files as $i => $path) {
    $rel = str_replace('\\', '/', substr($path, strlen($base) + 1));
    $zip->addFile($path, "$root/$rel");
    if ($i % 50 === 0) {
        $emit(['type' => 'progress', 'current' => $i, 'total' => $total, 'label' => 'adding files']);
        if (is_file((string) ($spec['cancelFile'] ?? ''))) {
            @unlink($tmp);
            exit(130);
        }
    }
}
$emit(['type' => 'progress', 'current' => $total, 'total' => $total, 'label' => 'compressing']);
if (!$zip->close()) {
    @unlink($tmp);
    fwrite(STDERR, "archive: could not write the ZIP\n");
    exit(3);
}
rename($tmp, $target);
$emit(['type' => 'result', 'files' => $total, 'bytes' => (int) filesize($target)]);
exit(0);
