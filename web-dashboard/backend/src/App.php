<?php
declare(strict_types=1);

namespace App;

use App\Controllers\ActivityController;
use App\Controllers\ContactsController;
use App\Controllers\CrawlsController;
use App\Controllers\DashboardController;
use App\Controllers\JobsController;
use App\Controllers\NotesController;
use App\Controllers\TodosController;
use App\Controllers\SitesController;
use App\Controllers\SnapshotsController;
use App\Controllers\TaxonomyController;
use App\Controllers\SystemController;
use App\Domain\ActivityLogger;
use App\Domain\CrawlStore;
use App\Domain\ProfileIngestor;
use App\Domain\ProfileNormalizer;
use App\Domain\PublicSuffixList;
use App\Domain\SnapshotDiff;
use App\Domain\UrlNormalizer;
use App\Http\Guard;
use App\Http\HttpException;
use App\Http\Request;
use App\Http\Response;
use App\Http\Router;
use App\Infra\Backup;
use App\Infra\Config;
use App\Infra\Database;
use App\Infra\Logger;
use App\Infra\Migrator;
use App\Infra\Paths;
use App\Jobs\JobLauncher;
use App\Jobs\JobRepository;
use PDO;

/** Tiny service container shared by the API, the worker and the CLI tools. */
final class App
{
    public readonly Paths $paths;
    private ?PDO $pdo = null;
    private ?JobRepository $jobs = null;

    public function __construct(public readonly Config $config)
    {
        $this->paths = new Paths($config->dataDir());
    }

    public static function boot(): self
    {
        return new self(Config::load());
    }

    public function pdo(): PDO
    {
        return $this->pdo ??= Database::open($this->paths);
    }

    public function logger(string $file = 'app.log'): Logger
    {
        return new Logger($this->paths, $file);
    }

    public function migrator(): Migrator
    {
        return new Migrator($this->pdo(), $this->config->string('backend_dir') . '/migrations', new Backup($this->pdo(), $this->paths));
    }

    public function jobs(): JobRepository
    {
        return $this->jobs ??= new JobRepository($this->pdo());
    }

    public function crawls(): CrawlStore
    {
        return new CrawlStore($this->pdo(), $this->paths, $this->jobs());
    }

    public function launcher(): JobLauncher
    {
        return new JobLauncher($this->config, $this->paths, $this->jobs());
    }

    public function version(): string
    {
        $info = $this->config->string('backend_dir') . '/build-info.php';
        if (is_file($info)) {
            $loaded = require $info;
            return (string) ($loaded['version'] ?? 'dev');
        }
        return 'dev';
    }

    public function urlNormalizer(): UrlNormalizer
    {
        return new UrlNormalizer(PublicSuffixList::bundled());
    }

    public function ingestor(): ProfileIngestor
    {
        return new ProfileIngestor($this->pdo(), new ProfileNormalizer(), new SnapshotDiff(), new ActivityLogger($this->pdo()));
    }

    /** Called by the worker after every job: turns a finished profile job into a snapshot. */
    public function onJobFinished(array $job, string $status): void
    {
        if ($job['type'] === 'crawl') {
            $this->crawls()->ingest($job, $status);
            return;
        }
        if ($job['type'] === 'archive') {
            $this->crawls()->ingestArchive($job, $status);
            return;
        }
        if ($job['type'] !== 'profile' || $job['site_id'] === null || $status === 'canceled') {
            return;
        }
        $siteId = (int) $job['site_id'];
        $jobId = (int) $job['id'];
        $file = $this->paths->data('jobs', (string) $jobId, 'profile.json');
        $ingestor = $this->ingestor();
        $text = is_file($file) ? file_get_contents($file) : false;
        $raw = $text !== false ? json_decode($text, true) : null;
        $result = is_array($raw)
            ? $ingestor->ingest($siteId, $jobId, $raw, $text)
            : $ingestor->ingestFailure($siteId, $jobId, $job['error'] ?: 'The profiler produced no readable report.');
        if ($result['outcome'] === 'failed' && is_array($raw)) {
            $first = (new ProfileNormalizer())->normalize($raw)['errors'][0]['message'] ?? null;
            if ($first) {
                $this->jobs()->setError($jobId, $first); // "unreachable: ..." reads better than "exited with code 3"
            }
        }
    }

    public function router(): Router
    {
        $router = new Router();
        $system = new SystemController($this);
        $jobs = new JobsController($this);
        $router->get('/api/health', [$system, 'health']);
        $router->get('/api/backups', [$system, 'listBackups']);
        $router->post('/api/backups', [$system, 'createBackup']);
        $router->post('/api/system/spawn-test', [$system, 'spawnTest']);
        $router->get('/api/jobs', [$jobs, 'list']);
        $router->get('/api/jobs/{id:\d+}', [$jobs, 'get']);
        $router->get('/api/jobs/{id:\d+}/log', [$jobs, 'log']);
        $router->post('/api/jobs/{id:\d+}/cancel', [$jobs, 'cancel']);
        $router->post('/api/jobs/{id:\d+}/retry', [$jobs, 'retry']);

        $sites = new SitesController($this);
        $snapshots = new SnapshotsController($this);
        $tax = new TaxonomyController($this);
        $router->get('/api/sites', [$sites, 'list']);
        $router->get('/api/sites/check-url', [$sites, 'checkUrl']);
        $router->post('/api/sites', [$sites, 'create']);
        $router->post('/api/sites/bulk', [$sites, 'bulk']);
        $router->get('/api/sites/{id:\d+}', [$sites, 'get']);
        $router->add('PATCH', '/api/sites/{id:\d+}', [$sites, 'patch']);
        $router->add('DELETE', '/api/sites/{id:\d+}', [$sites, 'delete']);
        $router->post('/api/sites/{id:\d+}/restore', [$sites, 'restore']);
        $router->post('/api/sites/{id:\d+}/fetch', [$sites, 'fetch']);
        $router->get('/api/sites/{id:\d+}/favicon', [$sites, 'favicon']);
        $router->get('/api/sites/{id:\d+}/snapshots', [$snapshots, 'list']);
        $router->get('/api/snapshots/{id:\d+}', [$snapshots, 'get']);
        $router->get('/api/snapshots/{id:\d+}/diff', [$snapshots, 'diff']);
        $router->add('DELETE', '/api/snapshots/{id:\d+}', [$snapshots, 'delete']);
        $router->get('/api/categories', [$tax, 'categories']);
        $router->post('/api/categories', [$tax, 'createCategory']);
        $router->add('PATCH', '/api/categories/{id:\d+}', [$tax, 'updateCategory']);
        $router->add('DELETE', '/api/categories/{id:\d+}', [$tax, 'deleteCategory']);
        $router->get('/api/tags', [$tax, 'tags']);
        $router->post('/api/tags', [$tax, 'createTag']);
        $router->add('PATCH', '/api/tags/{id:\d+}', [$tax, 'updateTag']);
        $router->add('DELETE', '/api/tags/{id:\d+}', [$tax, 'deleteTag']);
        $router->post('/api/tags/{id:\d+}/merge', [$tax, 'mergeTag']);
        $notes = new NotesController($this);
        $todos = new TodosController($this);
        $activity = new ActivityController($this);
        $contacts = new ContactsController($this);
        $router->get('/api/dashboard', [new DashboardController($this), 'get']);
        $router->get('/api/backups/{name}/download', [$system, 'downloadBackup']);
        $router->get('/api/sites/{id:\d+}/notes', [$notes, 'list']);
        $router->post('/api/sites/{id:\d+}/notes', [$notes, 'create']);
        $router->get('/api/notes/{id:\d+}', [$notes, 'get']);
        $router->add('PATCH', '/api/notes/{id:\d+}', [$notes, 'patch']);
        $router->add('DELETE', '/api/notes/{id:\d+}', [$notes, 'delete']);
        $router->get('/api/sites/{id:\d+}/todos', [$todos, 'forSite']);
        $router->post('/api/sites/{id:\d+}/todos', [$todos, 'create']);
        $router->get('/api/todos', [$todos, 'all']);
        $router->add('PATCH', '/api/todos/{id:\d+}', [$todos, 'patch']);
        $router->add('DELETE', '/api/todos/{id:\d+}', [$todos, 'delete']);
        $router->get('/api/sites/{id:\d+}/activity', [$activity, 'forSite']);
        $router->get('/api/activity', [$activity, 'all']);
        $router->get('/api/sites/{id:\d+}/contacts', [$contacts, 'contacts']);
        $router->post('/api/sites/{id:\d+}/contacts', [$contacts, 'createContact']);
        $router->add('PATCH', '/api/contacts/{id:\d+}', [$contacts, 'updateContact']);
        $router->add('DELETE', '/api/contacts/{id:\d+}', [$contacts, 'deleteContact']);
        $router->get('/api/sites/{id:\d+}/links', [$contacts, 'links']);
        $router->post('/api/sites/{id:\d+}/links', [$contacts, 'createLink']);
        $router->add('PATCH', '/api/links/{id:\d+}', [$contacts, 'updateLink']);
        $router->add('DELETE', '/api/links/{id:\d+}', [$contacts, 'deleteLink']);
        $crawls = new CrawlsController($this);
        $router->get('/api/sites/{id:\d+}/crawls', [$crawls, 'forSite']);
        $router->post('/api/sites/{id:\d+}/crawls', [$crawls, 'create']);
        $router->get('/api/crawls/{id:\d+}', [$crawls, 'get']);
        $router->get('/api/crawls/{id:\d+}/pages', [$crawls, 'pages']);
        $router->get('/api/crawls/{id:\d+}/pages/{pageId}', [$crawls, 'page']);
        $router->get('/api/crawls/{id:\d+}/assets', [$crawls, 'assets']);
        $router->get('/api/crawls/{id:\d+}/raw', [$crawls, 'raw']);
        $router->post('/api/crawls/{id:\d+}/archive', [$crawls, 'buildArchive']);
        $router->get('/api/crawls/{id:\d+}/archive', [$crawls, 'downloadArchive']);
        $router->post('/api/crawls/{id:\d+}/reveal', [$crawls, 'reveal']);
        $router->add('DELETE', '/api/crawls/{id:\d+}', [$crawls, 'delete']);
        $router->get('/api/views', [$tax, 'views']);
        $router->post('/api/views', [$tax, 'createView']);
        $router->add('PATCH', '/api/views/{id:\d+}', [$tax, 'updateView']);
        $router->add('DELETE', '/api/views/{id:\d+}', [$tax, 'deleteView']);
        return $router;
    }

    /** Runs one request; never throws. */
    public static function handle(Request $request): Response
    {
        try {
            $app = self::boot();
        } catch (\Throwable $e) {
            return Response::error(500, 'config_error', $e->getMessage());
        }
        try {
            $blocked = (new Guard($app->config->allowedHosts()))->check($request);
            if ($blocked !== null) {
                return $blocked;
            }
            $app->migrator()->migrate();
            return $app->router()->dispatch($request);
        } catch (HttpException $e) {
            return Response::error($e->status, $e->errorCode, $e->getMessage(), $e->fields, null, $e->extra);
        } catch (\Throwable $e) {
            $ref = bin2hex(random_bytes(4));
            try {
                $app->logger()->error($e::class . ': ' . $e->getMessage() . ' @ ' . $e->getFile() . ':' . $e->getLine() . ($app->config->isDev() ? "\n" . $e->getTraceAsString() : ''), "api#$ref");
            } catch (\Throwable) {
            }
            return Response::error(500, 'internal_error', $app->config->isDev() ? $e->getMessage() : 'Something went wrong.', [], $ref);
        }
    }
}
