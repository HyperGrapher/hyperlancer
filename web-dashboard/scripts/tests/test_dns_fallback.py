import dns.exception
import dns.resolver

from site_profile import dnsinfo


class FakeAnswer:
    rrset = None

    def __iter__(self):
        return iter([])


def test_public_resolvers_are_tried_when_the_system_one_fails(monkeypatch):
    seen: list[list[str]] = []

    def fake_resolve(self, *args, **kwargs):
        seen.append(list(self.nameservers))
        if self.nameservers != dnsinfo.PUBLIC_RESOLVERS:
            raise dns.exception.Timeout()
        return FakeAnswer()

    monkeypatch.setattr(dns.resolver.Resolver, "resolve", fake_resolve)
    values, error, nxdomain = dnsinfo.lookup("example.com", "A", 3, None)
    assert error is None and values == [] and not nxdomain
    assert seen[-1] == dnsinfo.PUBLIC_RESOLVERS


def test_a_chosen_resolver_is_not_replaced(monkeypatch):
    def fake_resolve(self, *args, **kwargs):
        raise dns.exception.Timeout()

    monkeypatch.setattr(dns.resolver.Resolver, "resolve", fake_resolve)
    _, error, _ = dnsinfo.lookup("example.com", "A", 3, "192.0.2.1")
    assert error is not None
