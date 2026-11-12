<?php
declare(strict_types=1);

namespace App\Http;

/** Host allow-list, JSON-only mutations with a custom header, Origin check (docs/DECISIONS.md D12). */
final class Guard
{
    public const CSRF_HEADER = 'freelance-dashboard';

    /** @param list<string> $allowedHosts lower-case, optionally with port */
    public function __construct(private readonly array $allowedHosts)
    {
    }

    public function check(Request $request): ?Response
    {
        $host = strtolower((string) $request->header('host'));
        if (!$this->hostAllowed($host)) {
            return Response::error(421, 'misdirected_request', sprintf(
                "Host '%s' is not allowed. Allowed: %s. Add it with APP_ALLOWED_HOSTS or 'allowed_hosts' in config.local.php.",
                $host === '' ? '(none)' : $host,
                implode(', ', $this->allowedHosts)
            ));
        }
        if (!$request->isMutation()) {
            return null;
        }
        $origin = $request->header('origin');
        if ($origin !== null && $origin !== 'null') {
            $originHost = strtolower((string) parse_url($origin, PHP_URL_HOST));
            $originPort = parse_url($origin, PHP_URL_PORT);
            $full = $originPort ? "$originHost:$originPort" : $originHost;
            if (!$this->hostAllowed($full)) {
                return Response::error(403, 'forbidden_origin', 'Origin not allowed.');
            }
        } elseif ($origin === 'null') {
            return Response::error(403, 'forbidden_origin', 'Origin not allowed.');
        }
        if ($request->header('x-requested-with') !== self::CSRF_HEADER) {
            return Response::error(403, 'csrf', 'Missing X-Requested-With header.');
        }
        // DELETE and bodiless POSTs (cancel, retry) may omit the body, but never send a non-JSON one.
        $type = strtolower((string) $request->header('content-type'));
        if ($type !== '' && !str_starts_with($type, 'application/json')) {
            return Response::error(415, 'unsupported_media_type', 'Content-Type must be application/json.');
        }
        return null;
    }

    private function hostAllowed(string $hostHeader): bool
    {
        if ($hostHeader === '') {
            return false;
        }
        $bare = preg_replace('/:\d+$/', '', $hostHeader);
        foreach ($this->allowedHosts as $allowed) {
            if ($allowed === $hostHeader || $allowed === $bare) {
                return true;
            }
        }
        return false;
    }
}
