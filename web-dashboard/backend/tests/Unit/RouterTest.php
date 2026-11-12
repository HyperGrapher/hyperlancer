<?php
declare(strict_types=1);

namespace App\Tests\Unit;

use App\Http\Request;
use App\Http\Response;
use App\Http\Router;
use PHPUnit\Framework\TestCase;

final class RouterTest extends TestCase
{
    private function request(string $method, string $path): Request
    {
        return new Request($method, $path, [], [], '');
    }

    public function testMatchesPatternParams(): void
    {
        $router = new Router();
        $router->get('/api/sites/{id:\d+}', static fn ($r, $p) => ['id' => (int) $p['id']]);
        $response = $router->dispatch($this->request('GET', '/api/sites/12/'));
        $this->assertSame(200, $response->status);
        $this->assertSame('{"id":12}', $response->body);
    }

    public function testNotFoundAndMethodNotAllowed(): void
    {
        $router = new Router();
        $router->get('/api/x', static fn () => Response::noContent());
        $this->assertSame(404, $router->dispatch($this->request('GET', '/api/y'))->status);
        $wrong = $router->dispatch($this->request('POST', '/api/x'));
        $this->assertSame(405, $wrong->status);
        $this->assertSame('GET', $wrong->headers['Allow']);
    }

    public function testNonNumericIdDoesNotMatch(): void
    {
        $router = new Router();
        $router->get('/api/sites/{id:\d+}', static fn () => ['ok' => true]);
        $this->assertSame(404, $router->dispatch($this->request('GET', '/api/sites/abc'))->status);
    }
}
