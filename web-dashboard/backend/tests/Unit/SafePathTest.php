<?php
declare(strict_types=1);

namespace App\Tests\Unit;

use App\Security\SafePath;
use App\Tests\TempDir;
use PHPUnit\Framework\Attributes\DataProvider;
use PHPUnit\Framework\TestCase;

final class SafePathTest extends TestCase
{
    private string $dir;

    protected function setUp(): void
    {
        $this->dir = TempDir::create();
        mkdir($this->dir . '/crawl/pages', 0775, true);
        file_put_contents($this->dir . '/crawl/pages/a.md', 'x');
        file_put_contents($this->dir . '/secret.txt', 's');
    }

    protected function tearDown(): void
    {
        TempDir::remove($this->dir);
    }

    public function testResolvesFilesInsideTheFolder(): void
    {
        $this->assertSame(realpath($this->dir . '/crawl/pages/a.md'), SafePath::resolve($this->dir . '/crawl', 'pages/a.md'));
    }

    #[DataProvider('bad')]
    public function testRejectsEscapes(string $path): void
    {
        $this->expectException(\InvalidArgumentException::class);
        SafePath::resolve($this->dir . '/crawl', $path);
    }

    /** @return array<string,array{string}> */
    public static function bad(): array
    {
        return [
            'parent' => ['../secret.txt'], 'nested parent' => ['pages/../../secret.txt'], 'absolute' => ['/etc/passwd'],
            'drive' => ['C:/Windows/win.ini'], 'backslash' => ['pages\\a.md'], 'nul' => ["pages/a.md\0.png"], 'empty' => [''],
            'directory' => ['pages'], 'missing' => ['pages/none.md'], 'dot' => ['./pages/a.md'],
        ];
    }

    public function testSymlinkIsRefused(): void
    {
        if (!@symlink($this->dir . '/secret.txt', $this->dir . '/crawl/link.txt')) {
            $this->markTestSkipped('symlinks unavailable');
        }
        $this->expectException(\InvalidArgumentException::class);
        SafePath::resolve($this->dir . '/crawl', 'link.txt');
    }
}
