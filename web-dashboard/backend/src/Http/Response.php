<?php
declare(strict_types=1);

namespace App\Http;

final class Response
{
    /** @param array<string,string> $headers */
    public function __construct(public readonly int $status, public readonly string $body = '', public readonly array $headers = [])
    {
    }

    public static function json(mixed $data, int $status = 200): self
    {
        return new self($status, (string) json_encode($data, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE | JSON_INVALID_UTF8_SUBSTITUTE | JSON_PRESERVE_ZERO_FRACTION), [
            'Content-Type' => 'application/json; charset=utf-8',
        ]);
    }

    public static function error(int $status, string $code, string $message, array $fields = [], ?string $logRef = null, array $extra = []): self
    {
        $error = ['code' => $code, 'message' => $message] + $extra;
        if ($fields) {
            $error['fields'] = $fields;
        }
        if ($logRef !== null) {
            $error['logRef'] = $logRef;
        }
        return self::json(['error' => $error], $status);
    }

    /** Streams a file as a download; $path must already be validated by the caller. */
    public static function file(string $path, string $downloadName, string $contentType = 'application/octet-stream'): self
    {
        return new self(200, '', [
            'Content-Type' => $contentType,
            'Content-Length' => (string) filesize($path),
            'Content-Disposition' => 'attachment; filename="' . preg_replace('/[^A-Za-z0-9._-]/', '_', $downloadName) . '"',
            '__file' => $path,
        ]);
    }

    /** Streams a validated file for display in the browser; the caller picks a content type that cannot run script. */
    public static function inlineFile(string $path, string $contentType, ?string $downloadName = null): self
    {
        $headers = [
            'Content-Type' => $contentType,
            'Content-Length' => (string) filesize($path),
            'Content-Security-Policy' => "sandbox; default-src 'none'; img-src data:; style-src 'unsafe-inline'",
            '__file' => $path,
        ];
        if ($downloadName !== null) {
            $headers['Content-Disposition'] = 'attachment; filename="' . preg_replace('/[^A-Za-z0-9._-]/', '_', $downloadName) . '"';
        }
        return new self(200, '', $headers);
    }

    public static function noContent(): self
    {
        return new self(204);
    }

    public function send(): void
    {
        http_response_code($this->status);
        header('Cache-Control: no-store');
        header('X-Content-Type-Options: nosniff');
        header('X-Frame-Options: DENY');
        header('Referrer-Policy: no-referrer');
        $file = $this->headers['__file'] ?? null;
        foreach ($this->headers as $name => $value) {
            if ($name !== '__file') {
                header("$name: $value");
            }
        }
        if ($file !== null) {
            readfile($file);
            return;
        }
        echo $this->body;
    }
}
