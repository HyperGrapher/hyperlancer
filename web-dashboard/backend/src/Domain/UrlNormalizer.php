<?php
declare(strict_types=1);

namespace App\Domain;

use InvalidArgumentException;

/** Same rules as the profiler's normalize_url (docs/PRD.md §5.1). */
final class UrlNormalizer
{
    public function __construct(private readonly PublicSuffixList $psl)
    {
    }

    /** @return array{url:string,host:string,siteKey:string,registrableDomain:?string,isIp:bool} */
    public function normalize(string $input): array
    {
        $value = trim($input);
        if ($value === '') {
            throw new InvalidArgumentException('Enter a website URL or hostname.');
        }
        if (preg_match('#^([a-z][a-z0-9+.-]*)://#i', $value, $m)) {
            if (!in_array(strtolower($m[1]), ['http', 'https'], true)) {
                throw new InvalidArgumentException('Only http and https URLs are supported.');
            }
            $scheme = strtolower($m[1]);
            $rest = substr($value, strlen($m[0]));
        } else {
            $scheme = 'https';
            $rest = ltrim($value, '/');
        }
        if (!preg_match('#^([^/?\#]*)([^\#]*)#u', $rest, $parts)) {
            throw new InvalidArgumentException('Enter a valid URL.');
        }
        $authority = $parts[1];
        $pathAndQuery = $parts[2];
        if (str_contains($authority, '@')) {
            throw new InvalidArgumentException('URLs containing a username or password are not supported.');
        }

        $port = null;
        if (preg_match('/^\[([0-9a-f:.]+)\](?::(\d{1,5}))?$/i', $authority, $m)) {
            $host = strtolower($m[1]);
            $port = isset($m[2]) && $m[2] !== '' ? (int) $m[2] : null;
            $isIp = filter_var($host, FILTER_VALIDATE_IP, FILTER_FLAG_IPV6) !== false;
            $shown = "[$host]";
            if (!$isIp) {
                throw new InvalidArgumentException('Enter a valid IPv6 address.');
            }
        } else {
            if (preg_match('/^(.*):(\d{1,5})$/u', $authority, $m)) {
                $authority = $m[1];
                $port = (int) $m[2];
            }
            $host = rtrim(mb_strtolower($authority, 'UTF-8'), '.');
            $isIp = filter_var($host, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4) !== false;
            if (!$isIp) {
                $ascii = function_exists('idn_to_ascii')
                    ? idn_to_ascii($host, IDNA_NONTRANSITIONAL_TO_ASCII, INTL_IDNA_VARIANT_UTS46)
                    : (preg_match('/^[\x00-\x7F]+$/', $host) ? $host : false);
                if ($ascii === false || $ascii === '') {
                    throw new InvalidArgumentException('Enter a valid hostname.');
                }
                $host = strtolower($ascii);
                if (!preg_match('/^(?=.{1,253}$)([a-z0-9_]([a-z0-9_-]{0,61}[a-z0-9_])?)(\.[a-z0-9_]([a-z0-9_-]{0,61}[a-z0-9_])?)*$/', $host)) {
                    throw new InvalidArgumentException('Enter a valid hostname.');
                }
            }
            $shown = $host;
        }
        if ($port !== null && ($port < 1 || $port > 65535)) {
            throw new InvalidArgumentException('Enter a valid port.');
        }

        $path = $pathAndQuery === '' ? '/' : $pathAndQuery;
        if ($path[0] !== '/') {
            $path = '/' . $path;
        }
        $path = preg_replace_callback('/[^A-Za-z0-9\-._~!$&\'()*+,;=:@\/?%]/u', static fn ($m) => rawurlencode($m[0]), $path) ?? $path;

        $defaultPort = $scheme === 'https' ? 443 : 80;
        $netloc = $shown . ($port !== null && $port !== $defaultPort ? ":$port" : '');
        $key = (str_starts_with($host, 'www.') && !$isIp ? substr($host, 4) : $host)
            . ($port !== null && $port !== $defaultPort ? ":$port" : '');

        return [
            'url' => "$scheme://$netloc$path",
            'host' => $host,
            'siteKey' => $key,
            'registrableDomain' => $isIp ? null : $this->psl->registrableDomain($host),
            'isIp' => $isIp,
        ];
    }
}
