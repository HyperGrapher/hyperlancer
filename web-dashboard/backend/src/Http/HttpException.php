<?php
declare(strict_types=1);

namespace App\Http;

final class HttpException extends \RuntimeException
{
    /** @param array<string,string> $fields @param array<string,mixed> $extra additional keys inside the error object */
    public function __construct(
        public readonly int $status,
        public readonly string $errorCode,
        string $message,
        public readonly array $fields = [],
        public readonly array $extra = [],
    ) {
        parent::__construct($message);
    }
}
