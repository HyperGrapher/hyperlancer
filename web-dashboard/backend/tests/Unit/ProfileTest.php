<?php
declare(strict_types=1);

namespace App\Tests\Unit;

use App\Domain\ActivityLogger;
use App\Domain\ProfileIngestor;
use App\Domain\ProfileNormalizer;
use App\Domain\SnapshotDiff;
use App\Infra\Database;
use App\Infra\Migrator;
use App\Infra\Paths;
use App\Tests\TempDir;
use PDO;
use PHPUnit\Framework\Attributes\DataProvider;
use PHPUnit\Framework\TestCase;

final class ProfileTest extends TestCase
{
    private const DIR = __DIR__ . '/../fixtures/profiles/';

    /** @return array<string,mixed> */
    private static function load(string $name): array
    {
        return json_decode((string) file_get_contents(self::DIR . $name), true, 512, JSON_THROW_ON_ERROR);
    }

    /** @return iterable<string,array{string}> */
    public static function v1Files(): iterable
    {
        foreach (['forazeytin.com.tr.json', 'oleoolivia.com.json', 'renama.com.tr.json', 'resoltenerji.com.json'] as $f) {
            yield $f => [$f];
        }
    }

    #[DataProvider('v1Files')]
    public function testNormalizesLegacyReports(string $file): void
    {
        $summary = (new ProfileNormalizer())->normalize(self::load($file));
        $this->assertSame(1, $summary['schemaVersion']);
        $this->assertContains($summary['outcome'], ['ok', 'partial', 'failed']);
        // Every top-level key the UI reads is present, whatever the schema version.
        foreach (['url', 'http', 'tech', 'cms', 'hosting', 'dns', 'tls', 'domain', 'seo', 'sitemaps', 'contact', 'security', 'archive', 'limitations'] as $key) {
            $this->assertArrayHasKey($key, $summary);
        }
        $this->assertSame(array_keys((new ProfileNormalizer())->normalize([])), array_keys($summary));
        $this->assertNotEmpty($summary['url']['requested']);
        $this->assertIsArray($summary['hosting']['ips']);
    }

    public function testLegacyForazeytinFacts(): void
    {
        $n = new ProfileNormalizer();
        $summary = $n->normalize(self::load('forazeytin.com.tr.json'));
        $facts = $n->facts($summary);
        $this->assertSame('Custom', $facts['cms']);
        $this->assertSame('PHP', $facts['backend_language']);
        $this->assertSame(200, $facts['http_status']);
        $this->assertSame('partial', $summary['outcome']);
        $this->assertNotEmpty($summary['errors']);
        $this->assertMatchesRegularExpression('/^\d{4}-\d{2}-\d{2}$/', (string) $facts['tls_valid_until']);
    }

    public function testV2ReportKeepsTheNewFields(): void
    {
        $summary = (new ProfileNormalizer())->normalize(self::load('v2-ok.json'));
        $this->assertSame(['outcome' => 'ok', 'schema' => 2], ['outcome' => $summary['outcome'], 'schema' => $summary['schemaVersion']]);
        $this->assertSame('WordPress', $summary['cms']['name']);
        $this->assertSame('6.6.1', $summary['cms']['version']);
        $this->assertSame('astra', $summary['cms']['theme']);
        $this->assertSame('Natro', $summary['dns']['emailProvider']);
        $this->assertSame(['ns1.natrohost.com'], $summary['dns']['ns']);
        $this->assertSame('registry', $summary['dns']['nsSource']);
        $this->assertSame('AS34619', $summary['hosting']['asn']);
        $this->assertSame('7.4.33', $summary['hosting']['languageVersion']);
        $this->assertSame('Sectigo', $summary['tls']['issuer']);
        $this->assertSame(['PHPSESSID'], array_column($summary['security']['cookies'], 'name'));
        $this->assertTrue($summary['security']['cookies'][0]['httpOnly']);
        $this->assertSame('2009-03-01', $summary['archive']['first']);
        $this->assertSame('2018-08', $summary['design']['estimatedFrom']);
        $this->assertContains('archive', array_column($summary['design']['clues'], 'kind'));
        $this->assertContains('info@forazeytin.com.tr', $summary['contact']['emails']);
        $facts = (new ProfileNormalizer())->facts($summary);
        $this->assertSame('LiteSpeed', $facts['server']);
        $this->assertSame('89.19.30.15', $facts['ip_address']);
        $this->assertSame('2027-07-05', $facts['domain_expires_on']);
    }

    public function testEmptyAndFailedInputsKeepTheShape(): void
    {
        $n = new ProfileNormalizer();
        $failed = $n->failed('worker crashed');
        $this->assertSame('failed', $failed['outcome']);
        $this->assertSame('worker crashed', $failed['errors'][0]['message']);
        $this->assertNull($failed['cms']['name']);
        $this->assertSame(array_keys($n->normalize([])), array_keys($failed));
    }

    public function testDiffReportsSemanticChangesOnly(): void
    {
        $n = new ProfileNormalizer();
        $a = $n->normalize(self::load('v2-ok.json'));
        $this->assertSame(0, (new SnapshotDiff())->diff($a, $a)['count']);

        $b = $a;
        $b['tech'][] = ['category' => 'Hosting / edge', 'name' => 'Cloudflare', 'version' => null, 'confidence' => 'high', 'evidence' => []];
        $b['hosting']['ips'] = ['104.21.4.9'];
        $b['dns']['ns'] = ['ada.ns.cloudflare.com'];
        $b['tls']['validUntil'] = '2027-03-01T00:00:00+00:00';
        $b['seo']['title'] = 'New title';
        $b['http']['ttfbMs'] = 999; // noise
        $b['tech'][0]['evidence'] = ['changed evidence']; // noise
        $result = (new SnapshotDiff())->diff($a, $b);

        $bySection = array_column($result['groups'], 'changes', 'section');
        $this->assertEqualsCanonicalizing(['Technologies', 'Hosting', 'DNS and email', 'SSL / TLS', 'SEO'], array_keys($bySection));
        $this->assertSame('added', $bySection['Technologies'][0]['kind']);
        $this->assertSame('Cloudflare', $bySection['Technologies'][0]['after']);
        $kinds = array_column($bySection['Hosting'], 'kind');
        $this->assertSame(['removed', 'added'], $kinds);
        $this->assertSame('2027-03-01', $bySection['SSL / TLS'][0]['after']);
        $this->assertContains('Domain', $result['unchangedSections']);
        $this->assertSame(7, $result['count']); // tech +1, IP -1/+1, NS -1/+1, valid-until, title
    }

    public function testDiffMarksVersionChanges(): void
    {
        $a = ['tech' => [['category' => 'CMS', 'name' => 'WordPress', 'version' => '6.5', 'confidence' => 'high', 'evidence' => []]]];
        $b = ['tech' => [['category' => 'CMS', 'name' => 'WordPress', 'version' => '6.6', 'confidence' => 'high', 'evidence' => []]]];
        $change = (new SnapshotDiff())->diff($a, $b)['groups'][0]['changes'][0];
        $this->assertSame(['changed', '6.5', '6.6'], [$change['kind'], $change['before'], $change['after']]);
    }

    // ---- ingestion -------------------------------------------------------------------------------------------------

    public function testIngestStoresSnapshotsFactsAndChangeCounts(): void
    {
        $dir = TempDir::create();
        try {
            $pdo = Database::open(new Paths($dir));
            (new Migrator($pdo, dirname(__DIR__, 2) . '/migrations'))->migrate();
            $pdo->exec("INSERT INTO sites (id, site_key, url) VALUES (1, 'forazeytin.com.tr', 'https://forazeytin.com.tr/')");
            $ingestor = new ProfileIngestor($pdo, new ProfileNormalizer(), new SnapshotDiff(), new ActivityLogger($pdo));

            $raw = self::load('v2-ok.json');
            $first = $ingestor->ingest(1, null, $raw);
            $this->assertSame(['ok', null], [$first['outcome'], $first['changes']]);
            $site = $pdo->query('SELECT * FROM sites WHERE id = 1')->fetch();
            $this->assertSame('WordPress', $site['cms']);
            $this->assertSame('ok', $site['last_fetch_outcome']);
            $this->assertSame((string) $first['snapshotId'], (string) $site['latest_snapshot_id']);
            $this->assertGreaterThan(0, (int) $pdo->query('SELECT count(*) FROM snapshot_technologies')->fetchColumn());

            $raw['page']['title'] = 'Changed title';
            $second = $ingestor->ingest(1, null, $raw);
            $this->assertSame(1, $second['changes']);

            // A failed attempt is recorded but does not replace the profile shown.
            $failed = $ingestor->ingestFailure(1, null, 'worker crashed');
            $this->assertSame('failed', $failed['outcome']);
            $site = $pdo->query('SELECT * FROM sites WHERE id = 1')->fetch();
            $this->assertSame('failed', $site['last_fetch_outcome']);
            $this->assertSame((string) $second['snapshotId'], (string) $site['latest_snapshot_id']);
            $this->assertSame('WordPress', $site['cms'], 'facts of the last good profile are kept');

            $messages = $pdo->query('SELECT message FROM activity ORDER BY id')->fetchAll(PDO::FETCH_COLUMN);
            $this->assertSame(['Profile fetched (ok)', 'Profile fetched (ok), 1 change', 'Profile fetch failed: worker crashed'], $messages);
        } finally {
            unset($pdo);
            TempDir::remove($dir);
        }
    }
}
