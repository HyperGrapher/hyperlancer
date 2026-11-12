<?php
declare(strict_types=1);

namespace App\Tests\Unit;

use App\Infra\Config;
use App\Infra\Paths;
use App\Tests\TempDir;
use PHPUnit\Framework\TestCase;

final class PathsConfigTest extends TestCase
{
    public function testPathsRejectTraversal(): void
    {
        $paths = new Paths('/data');
        $this->assertSame(implode(DIRECTORY_SEPARATOR, ['/data', 'crawls', 'a']), $paths->data('crawls', 'a'));
        $this->expectException(\InvalidArgumentException::class);
        $paths->data('crawls', '../etc');
    }

    public function testConfigRefusesDataDirInsideBuild(): void
    {
        $root = TempDir::create();
        try {
            mkdir("$root/dist/backend", 0775, true);
            file_put_contents("$root/dist/backend/build-info.php", "<?php return ['project_root' => '" . addslashes($root) . "'];");
            putenv('APP_DATA_DIR=' . $root . '/dist/data');
            $this->expectException(\RuntimeException::class);
            Config::load("$root/dist/backend");
        } finally {
            putenv('APP_DATA_DIR');
            TempDir::remove($root);
        }
    }

    public function testBuildInfoDecidesProjectRootAndDataDir(): void
    {
        $root = TempDir::create();
        try {
            mkdir("$root/dist/backend", 0775, true);
            file_put_contents("$root/dist/backend/build-info.php", "<?php return ['project_root' => '" . addslashes($root) . "'];");
            putenv('APP_DATA_DIR');
            $config = Config::load("$root/dist/backend");
            $this->assertSame($root, $config->string('project_root'));
            $this->assertSame($root . DIRECTORY_SEPARATOR . 'data', $config->dataDir());
        } finally {
            TempDir::remove($root);
        }
    }
}
