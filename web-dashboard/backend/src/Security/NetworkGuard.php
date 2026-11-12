<?php
declare(strict_types=1);

namespace App\Security;

/** Pre-check mirroring scripts/site_tools/netguard.py: refuse private, loopback and link-local targets. */
final class NetworkGuard
{
    /** @return string|null 'private_network' when the host must not be fetched, else null */
    public function blockedReason(string $host, bool $allowPrivate = false): ?string
    {
        if ($allowPrivate) {
            return null;
        }
        $host = trim($host, '[]');
        $addresses = filter_var($host, FILTER_VALIDATE_IP) !== false ? [$host] : $this->resolve($host);
        foreach ($addresses as $address) {
            if (self::isPrivate($address)) {
                return 'private_network';
            }
        }
        return null;
    }

    /** @return list<string> */
    private function resolve(string $host): array
    {
        $found = [];
        try {
            $v4 = @gethostbynamel($host);
            $found = $v4 ?: [];
            if (function_exists('dns_get_record')) {
                foreach (@dns_get_record($host, DNS_AAAA) ?: [] as $record) {
                    if (isset($record['ipv6'])) {
                        $found[] = $record['ipv6'];
                    }
                }
            }
        } catch (\Throwable) {
        }
        return $found; // unresolvable hosts pass: the fetch reports the DNS failure
    }

    public static function isPrivate(string $ip): bool
    {
        $packed = @inet_pton($ip);
        if ($packed === false) {
            return true;
        }
        if (strlen($packed) === 16) {
            // IPv4-mapped (::ffff:a.b.c.d): judge the embedded IPv4 address.
            if (str_starts_with($packed, str_repeat("\0", 10) . "\xff\xff")) {
                return self::isPrivate(inet_ntop(substr($packed, 12)) ?: '');
            }
            $first = ord($packed[0]);
            $second = ord($packed[1]);
            return $packed === str_repeat("\0", 16)                               // ::
                || $packed === str_repeat("\0", 15) . "\1"                         // ::1
                || ($first & 0xFE) === 0xFC                                        // fc00::/7 unique local
                || ($first === 0xFE && ($second & 0xC0) === 0x80)                  // fe80::/10 link-local
                || $first === 0xFF                                                 // multicast
                || ($first === 0x20 && $second === 0x01 && ord($packed[2]) === 0x0D && ord($packed[3]) === 0xB8); // documentation
        }
        [$a, $b] = [ord($packed[0]), ord($packed[1])];
        return $a === 0 || $a === 10 || $a === 127 || $a >= 224                      // this-net, RFC1918, loopback, multicast/reserved
            || ($a === 172 && $b >= 16 && $b <= 31)
            || ($a === 192 && $b === 168)
            || ($a === 169 && $b === 254)                                              // link-local, cloud metadata
            || ($a === 100 && $b >= 64 && $b <= 127);                                  // CGNAT
    }
}
