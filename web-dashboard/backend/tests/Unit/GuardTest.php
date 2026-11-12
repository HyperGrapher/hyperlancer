<?php
declare(strict_types=1);

namespace App\Tests\Unit;

use App\Http\Guard;
use App\Http\Request;
use PHPUnit\Framework\TestCase;

final class GuardTest extends TestCase
{
    private Guard $guard;

    protected function setUp(): void
    {
        $this->guard = new Guard(['dashboard.test', 'localhost']);
    }

    /** @param array<string,string> $headers */
    private function check(string $method, array $headers): ?int
    {
        return $this->guard->check(new Request($method, '/api/x', [], $headers, ''))?->status;
    }

    public function testRejectsUnknownHost(): void
    {
        $this->assertSame(421, $this->check('GET', ['host' => 'evil.example']));
        $this->assertSame(421, $this->check('GET', []));
    }

    public function testAllowsKnownHostWithPort(): void
    {
        $this->assertNull($this->check('GET', ['host' => 'localhost:5173']));
        $this->assertNull($this->check('GET', ['host' => 'dashboard.test']));
    }

    public function testMutationNeedsCustomHeader(): void
    {
        $this->assertSame(403, $this->check('POST', ['host' => 'localhost', 'content-type' => 'application/json']));
        $this->assertNull($this->check('POST', ['host' => 'localhost', 'content-type' => 'application/json', 'x-requested-with' => 'freelance-dashboard']));
    }

    public function testMutationRejectsForeignOriginAndFormBodies(): void
    {
        $ok = ['host' => 'localhost', 'x-requested-with' => 'freelance-dashboard', 'content-type' => 'application/json'];
        $this->assertSame(403, $this->check('POST', $ok + ['origin' => 'https://evil.example']));
        $this->assertSame(403, $this->check('POST', ['origin' => 'null'] + $ok));
        $this->assertNull($this->check('POST', $ok + ['origin' => 'http://localhost:5173']));
        $this->assertSame(415, $this->check('POST', ['content-type' => 'text/plain'] + $ok));
    }
}
