<?php
declare(strict_types=1);

namespace App\Domain;

/** Registrable domain (eTLD+1) from the bundled Public Suffix List, ICANN section only (like the profiler). */
final class PublicSuffixList
{
    /** @var array<string,true> */
    private array $rules = [];

    public function __construct(string $file)
    {
        $inIcann = false;
        foreach (file($file, FILE_IGNORE_NEW_LINES) ?: [] as $line) {
            $line = trim($line);
            if (str_contains($line, '===BEGIN ICANN DOMAINS===')) {
                $inIcann = true;
                continue;
            }
            if (str_contains($line, '===END ICANN DOMAINS===')) {
                $inIcann = false;
                continue;
            }
            if (!$inIcann || $line === '' || str_starts_with($line, '//')) {
                continue;
            }
            $this->rules[strtolower(strtok($line, " \t"))] = true;
        }
    }

    public static function bundled(): self
    {
        static $instance = null;
        return $instance ??= new self(dirname(__DIR__, 2) . '/resources/public_suffix_list.dat');
    }

    /** Number of labels in the public suffix of the (lower-case, ASCII) host. */
    public function suffixLength(string $host): int
    {
        $labels = explode('.', $host);
        $count = count($labels);
        $best = 1; // implicit "*" rule
        for ($k = 1; $k <= $count; $k++) {
            $suffix = implode('.', array_slice($labels, $count - $k));
            if (isset($this->rules['!' . $suffix])) {
                return $k - 1; // exception rule wins outright
            }
            $wildcard = $k > 1 ? '*.' . implode('.', array_slice($labels, $count - $k + 1)) : null;
            if (isset($this->rules[$suffix]) || ($wildcard !== null && isset($this->rules[$wildcard]))) {
                $best = max($best, $k);
            }
        }
        return $best;
    }

    public function registrableDomain(string $host): ?string
    {
        $labels = explode('.', $host);
        $length = $this->suffixLength($host);
        if (count($labels) <= $length) {
            return null; // the host is itself a public suffix
        }
        return implode('.', array_slice($labels, -($length + 1)));
    }
}
