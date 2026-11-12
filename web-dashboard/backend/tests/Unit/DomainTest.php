<?php
declare(strict_types=1);

namespace App\Tests\Unit;

use App\Domain\PublicSuffixList;
use App\Domain\UrlNormalizer;
use App\Security\NetworkGuard;
use InvalidArgumentException;
use PHPUnit\Framework\Attributes\DataProvider;
use PHPUnit\Framework\TestCase;

final class DomainTest extends TestCase
{
    private UrlNormalizer $normalizer;

    protected function setUp(): void
    {
        $this->normalizer = new UrlNormalizer(PublicSuffixList::bundled());
    }

    /** @return iterable<string,array{string,string,string,?string}> */
    public static function urls(): iterable
    {
        yield 'bare domain' => ['Example.COM', 'https://example.com/', 'example.com', 'example.com'];
        yield 'www is dropped from the key' => ['www.forazeytin.com.tr/path?x=1', 'https://www.forazeytin.com.tr/path?x=1', 'forazeytin.com.tr', 'forazeytin.com.tr'];
        yield 'idn' => ['http://çiçek.com.tr', 'http://xn--iek-1lab.com.tr/', 'xn--iek-1lab.com.tr', 'xn--iek-1lab.com.tr'];
        yield 'fragment dropped, default port dropped' => ['https://example.org:443/a#frag', 'https://example.org/a', 'example.org', 'example.org'];
        yield 'non-default port kept' => ['http://shop.example.co.uk:8080', 'http://shop.example.co.uk:8080/', 'shop.example.co.uk:8080', 'example.co.uk'];
        yield 'private suffix is not a registrable boundary' => ['foo.github.io', 'https://foo.github.io/', 'foo.github.io', 'github.io'];
        yield 'wildcard rule' => ['a.b.ck', 'https://a.b.ck/', 'a.b.ck', 'a.b.ck'];
        yield 'wildcard exception' => ['www.ck', 'https://www.ck/', 'ck', 'www.ck'];
        yield 'ipv4' => ['http://192.168.1.10/x', 'http://192.168.1.10/x', '192.168.1.10', null];
        yield 'trailing dot' => ['example.com.', 'https://example.com/', 'example.com', 'example.com'];
    }

    #[DataProvider('urls')]
    public function testNormalize(string $input, string $url, string $key, ?string $registrable): void
    {
        $n = $this->normalizer->normalize($input);
        $this->assertSame($url, $n['url']);
        $this->assertSame($key, $n['siteKey']);
        $this->assertSame($registrable, $n['registrableDomain']);
    }

    /** @return iterable<string,array{string}> */
    public static function badUrls(): iterable
    {
        foreach (['', '   ', 'ftp://example.com', 'https://user:pw@example.com', 'exa mple.com', 'https://-bad-.com', 'example.com:99999', 'javascript:alert(1)'] as $bad) {
            yield $bad => [$bad];
        }
    }

    #[DataProvider('badUrls')]
    public function testRejectsInvalidInput(string $input): void
    {
        $this->expectException(InvalidArgumentException::class);
        $this->normalizer->normalize($input);
    }

    public function testPublicSuffixLength(): void
    {
        $psl = PublicSuffixList::bundled();
        $this->assertSame(2, $psl->suffixLength('forazeytin.com.tr'));
        $this->assertSame('com.tr', implode('.', array_slice(explode('.', 'forazeytin.com.tr'), -2)));
        $this->assertNull($psl->registrableDomain('com.tr'));
        $this->assertSame('example.com', $psl->registrableDomain('a.b.example.com'));
    }

    /** @return iterable<string,array{string,bool}> */
    public static function addresses(): iterable
    {
        foreach (['127.0.0.1', '10.0.0.1', '172.16.5.5', '172.31.255.255', '192.168.0.1', '169.254.169.254', '100.64.0.1', '0.0.0.0', '224.0.0.1', '255.255.255.255', '::1', '::', 'fd00::1', 'fe80::1', 'ff02::1', '::ffff:127.0.0.1', '2001:db8::1', 'garbage'] as $ip) {
            yield $ip => [$ip, true];
        }
        foreach (['93.184.216.34', '1.1.1.1', '172.32.0.1', '100.128.0.1', '2606:4700:4700::1111', '::ffff:8.8.8.8'] as $ip) {
            yield $ip => [$ip, false];
        }
    }

    #[DataProvider('addresses')]
    public function testNetworkGuardClassifiesAddresses(string $ip, bool $private): void
    {
        $this->assertSame($private, NetworkGuard::isPrivate($ip));
    }

    public function testNetworkGuardLiteralHosts(): void
    {
        $guard = new NetworkGuard();
        $this->assertSame('private_network', $guard->blockedReason('192.168.1.1'));
        $this->assertSame('private_network', $guard->blockedReason('[::1]'));
        $this->assertNull($guard->blockedReason('192.168.1.1', true));
        $this->assertNull($guard->blockedReason('93.184.216.34'));
    }
}
