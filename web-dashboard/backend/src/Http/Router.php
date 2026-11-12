<?php
declare(strict_types=1);

namespace App\Http;

/** Patterns like `/api/sites/{id:\d+}`; a bare `{name}` matches one path segment. */
final class Router
{
    /** @var list<array{string,string,callable}> */
    private array $routes = [];

    public function add(string $method, string $pattern, callable $handler): void
    {
        $regex = preg_replace_callback('/\{(\w+)(?::([^}]+))?\}/', static fn ($m) => '(?P<' . $m[1] . '>' . ($m[2] ?? '[^/]+') . ')', $pattern);
        $this->routes[] = [strtoupper($method), '#^' . $regex . '$#', $handler];
    }

    public function get(string $pattern, callable $handler): void
    {
        $this->add('GET', $pattern, $handler);
    }

    public function post(string $pattern, callable $handler): void
    {
        $this->add('POST', $pattern, $handler);
    }

    public function dispatch(Request $request): Response
    {
        $path = $request->path === '/' ? '/' : rtrim($request->path, '/');
        $allowed = [];
        foreach ($this->routes as [$method, $regex, $handler]) {
            if (!preg_match($regex, $path, $m)) {
                continue;
            }
            if ($method !== $request->method) {
                $allowed[] = $method;
                continue;
            }
            $params = array_filter($m, 'is_string', ARRAY_FILTER_USE_KEY);
            $result = $handler($request, $params);
            return $result instanceof Response ? $result : Response::json($result);
        }
        if ($allowed) {
            return new Response(405, (string) json_encode(['error' => ['code' => 'method_not_allowed', 'message' => 'Method not allowed.']]), [
                'Content-Type' => 'application/json; charset=utf-8',
                'Allow' => implode(', ', array_unique($allowed)),
            ]);
        }
        return Response::error(404, 'not_found', 'No such endpoint.');
    }
}
