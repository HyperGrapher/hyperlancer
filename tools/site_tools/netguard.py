"""SSRF guard: refuse targets that are not on the public internet (docs/PRD.md §11).

Every host is resolved and every resulting address must be globally routable, unless the site is
explicitly marked as a local/LAN target. Callers check the initial URL and every redirect hop.
There is a small DNS-rebinding window between this check and the connection; acceptable for a
single-user local tool.
"""

from __future__ import annotations

import ipaddress
import socket
import urllib.parse

DEFAULT_PORTS = frozenset({80, 443, 8080, 8443})


class BlockedTarget(Exception):
    """The URL points somewhere the policy does not allow."""

    def __init__(self, message: str, reason: str) -> None:
        super().__init__(message)
        self.reason = reason


def is_blocked_ip(value: str) -> bool:
    ip = ipaddress.ip_address(value.split("%", 1)[0])
    if isinstance(ip, ipaddress.IPv6Address) and ip.ipv4_mapped is not None:
        ip = ip.ipv4_mapped
    # is_global is False for loopback, RFC 1918, link-local, CGNAT (100.64/10), ULA, multicast, reserved.
    return ip.is_multicast or not ip.is_global


def resolve(host: str) -> list[str]:
    try:
        infos = socket.getaddrinfo(host, None, type=socket.SOCK_STREAM)
    except socket.gaierror:
        return []
    return sorted({str(info[4][0]) for info in infos})


def check_url(url: str, allow_private: bool = False, ports: frozenset[int] = DEFAULT_PORTS) -> None:
    """Raise BlockedTarget if the URL must not be fetched. Unresolvable hosts pass: the fetch reports DNS errors."""
    if allow_private:
        return
    parts = urllib.parse.urlsplit(url)
    host = (parts.hostname or "").rstrip(".")
    if not host:
        raise BlockedTarget("URL has no host.", "invalid")
    port = parts.port or (443 if parts.scheme == "https" else 80)
    if port not in ports:
        raise BlockedTarget(f"Port {port} is not allowed.", "port")
    try:
        addresses = [str(ipaddress.ip_address(host))]
    except ValueError:
        addresses = resolve(host)
    for address in addresses:
        if is_blocked_ip(address):
            raise BlockedTarget(f"{host} resolves to a non-public address ({address}).", "private_network")
