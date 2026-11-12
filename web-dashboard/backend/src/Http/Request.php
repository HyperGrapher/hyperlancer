<?php
declare(strict_types=1);

namespace App\Http;

final class Request
{
    /** @param array<string,string> $headers lower-cased names */
    public function __construct(
        public readonly string $method,
        public readonly string $path,
        public readonly array $query,
        public readonly array $headers,
        private readonly string $rawBody,
    ) {
    }

    public static function fromGlobals(): self
    {
        $headers = [];
        foreach ($_SERVER as $key => $value) {
            if (str_starts_with($key, 'HTTP_')) {
                $headers[strtolower(str_replace('_', '-', substr($key, 5)))] = (string) $value;
            }
        }
        // Some FastCGI setups do not pass the Host header; fall back to what the server says it is serving.
        if (!isset($headers['host'])) {
            $fallback = $headers['x-forwarded-host'] ?? (string) ($_SERVER['SERVER_NAME'] ?? '');
            if ($fallback !== '') {
                $headers['host'] = $fallback;
            }
        }
        if (isset($_SERVER['CONTENT_TYPE'])) {
            $headers['content-type'] = (string) $_SERVER['CONTENT_TYPE'];
        }
        $uri = (string) ($_SERVER['REQUEST_URI'] ?? '/');
        $path = rawurldecode((string) (parse_url($uri, PHP_URL_PATH) ?: '/'));
        // Fallback entry point: /backend/index.php?r=/sites/1 (docs/PRD.md §8.1).
        if (isset($_GET['r']) && is_string($_GET['r'])) {
            $path = '/api/' . ltrim($_GET['r'], '/');
        }
        // `/index.php/api/...` form.
        if (str_starts_with($path, '/index.php/')) {
            $path = substr($path, strlen('/index.php'));
        }
        $query = $_GET;
        unset($query['r']);
        return new self(strtoupper((string) ($_SERVER['REQUEST_METHOD'] ?? 'GET')), $path, $query, $headers, (string) file_get_contents('php://input'));
    }

    public function header(string $name): ?string
    {
        return $this->headers[strtolower($name)] ?? null;
    }

    /** @return array<string,mixed> */
    public function json(): array
    {
        if (trim($this->rawBody) === '') {
            return [];
        }
        $data = json_decode($this->rawBody, true);
        if (!is_array($data)) {
            throw new HttpException(400, 'bad_json', 'Request body must be a JSON object.');
        }
        return $data;
    }

    public function isMutation(): bool
    {
        return in_array($this->method, ['POST', 'PATCH', 'PUT', 'DELETE'], true);
    }
}
