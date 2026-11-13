from __future__ import annotations

import datetime as dt
import hashlib
import os
import socket
import ssl
import tempfile
from typing import Any

import certifi


def flatten_certificate_name(value: Any) -> str | None:
    if not value:
        return None
    fields = [f"{key}={val}" for section in value for key, val in section]
    return ", ".join(fields) or None


def empty(error: str | None = None) -> dict[str, Any]:
    return {
        "connection_verified": False,
        "subject": None,
        "issuer": None,
        "serial_number": None,
        "valid_from_utc": None,
        "valid_until_utc": None,
        "days_until_expiry": None,
        "dns_subject_alt_name_count": 0,
        "dns_subject_alt_names_sample": [],
        "sha256_fingerprint": None,
        "cipher": None,
        "error": error,
    }


def _decode_der(der: bytes) -> dict[str, Any]:
    """Decode an unverified certificate. ssl only returns details for verified peers, so go through CPython's
    PEM test decoder (a private but long-stable helper). Returns {} when unavailable."""
    pem = ssl.DER_cert_to_PEM_cert(der)
    handle, path = tempfile.mkstemp(suffix=".pem")
    try:
        with os.fdopen(handle, "w") as fh:
            fh.write(pem)
        return ssl._ssl._test_decode_cert(path)  # type: ignore[attr-defined]
    except Exception:  # noqa: BLE001
        return {}
    finally:
        os.unlink(path)


def _build(certificate: dict[str, Any], der: bytes, cipher: Any, verified: bool, error: str | None) -> dict[str, Any]:
    not_after = ssl.cert_time_to_seconds(certificate["notAfter"]) if certificate.get("notAfter") else None
    not_before = ssl.cert_time_to_seconds(certificate["notBefore"]) if certificate.get("notBefore") else None
    now = dt.datetime.now(dt.timezone.utc).timestamp()
    names = [value for kind, value in certificate.get("subjectAltName", []) if kind == "DNS"]
    result = empty(error)
    result.update(
        connection_verified=verified,
        subject=flatten_certificate_name(certificate.get("subject")),
        issuer=flatten_certificate_name(certificate.get("issuer")),
        serial_number=certificate.get("serialNumber"),
        valid_from_utc=dt.datetime.fromtimestamp(not_before, dt.timezone.utc).isoformat() if not_before else None,
        valid_until_utc=dt.datetime.fromtimestamp(not_after, dt.timezone.utc).isoformat() if not_after else None,
        days_until_expiry=round((not_after - now) / 86400, 1) if not_after else None,
        dns_subject_alt_name_count=len(names),
        dns_subject_alt_names_sample=names[:20],
        sha256_fingerprint=hashlib.sha256(der).hexdigest(),
        cipher=list(cipher) if cipher else None,
    )
    return result


def _connect(host: str, port: int, timeout: float, context: ssl.SSLContext) -> tuple[dict[str, Any], bytes, Any]:
    with socket.create_connection((host, port), timeout=timeout) as raw:
        with context.wrap_socket(raw, server_hostname=host) as secure:
            der = secure.getpeercert(binary_form=True) or b""
            certificate = secure.getpeercert() or _decode_der(der)
            return certificate, der, secure.cipher()


def _contexts() -> list[ssl.SSLContext]:
    """Mozilla's bundle (what httpx trusts) and the operating system's store. A chain is accepted if either
    trusts it: Windows often lacks recent roots, other systems lack private ones."""
    contexts = [ssl.create_default_context(cafile=certifi.where())]
    try:
        contexts.append(ssl.create_default_context())
    except ssl.SSLError:
        pass
    return contexts


def profile_tls(host: str, port: int, timeout: float) -> dict[str, Any]:
    """Verified handshake first; if verification fails, read the certificate anyway and flag it as unverified."""
    verify_error = ""
    for context in _contexts():
        try:
            certificate, der, cipher = _connect(host, port, timeout, context)
            return _build(certificate, der, cipher, True, None)
        except ssl.SSLError as exc:
            verify_error = verify_error or f"{type(exc).__name__}: {exc}"
        except Exception as exc:  # noqa: BLE001
            return empty(f"{type(exc).__name__}: {exc}")
    try:
        context = ssl.create_default_context()
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        certificate, der, cipher = _connect(host, port, timeout, context)
        return _build(certificate, der, cipher, False, verify_error)
    except Exception as exc:  # noqa: BLE001
        return empty(f"{verify_error}; unverified retry failed: {type(exc).__name__}: {exc}")
