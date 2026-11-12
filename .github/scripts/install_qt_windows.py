"""Installs a Qt desktop kit for Windows from download.qt.io, for CI.

Qt 6.11 moved its Windows packages into per-compiler folders
(online/qtsdkrepository/windows_x86/desktop/qt6_6113/qt6_6113_mingw/), which aqtinstall 3.3
does not read yet. This script reads that folder's Updates.xml, downloads the archives of the
base package and the requested add-on modules, checks their SHA-1 and extracts them with 7-Zip.

Usage: python install_qt_windows.py --version 6.11.3 --arch win64_mingw --flavor mingw
                                   --modules qtshadertools qtwebsockets --output D:/Qt
Prints the kit folder (for example D:/Qt/6.11.3/mingw_64) on the last line.
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
import tempfile
import time
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

BASE = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop"
SKIPPED_ARCHIVES = ("qtdoc-",)  # documentation is large and not needed to build


def fetch(url: str, attempts: int = 4) -> bytes:
    for attempt in range(1, attempts + 1):
        try:
            with urllib.request.urlopen(url, timeout=120) as response:
                return response.read()
        except OSError as error:
            if attempt == attempts:
                raise
            print(f"retry {attempt} for {url}: {error}", file=sys.stderr)
            time.sleep(2 ** attempt)
    raise RuntimeError("unreachable")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--arch", required=True)
    parser.add_argument("--flavor", required=True, help="folder suffix, e.g. mingw or msvc2022_64")
    parser.add_argument("--modules", nargs="*", default=[])
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    digits = args.version.replace(".", "")
    folder = f"{BASE}/qt6_{digits}/qt6_{digits}_{args.flavor}"
    index = ET.fromstring(fetch(f"{folder}/Updates.xml"))
    packages = {p.findtext("Name"): p for p in index.findall("PackageUpdate")}

    wanted = [f"qt.qt6.{digits}.{args.arch}"]
    wanted += [f"qt.qt6.{digits}.addons.{module}.{args.arch}" for module in args.modules]
    # Archives in this layout hold the kit's contents directly (bin/, lib/, ...), so they are
    # extracted into <output>/<version>/<kit>, the folder the Qt installer would use.
    kit_name = "mingw_64" if args.flavor == "mingw" else args.flavor
    output = Path(args.output) / args.version / kit_name
    output.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as temp:
        for name in wanted:
            package = packages.get(name)
            if package is None:
                print(f"error: package {name} not found in {folder}/Updates.xml", file=sys.stderr)
                return 1
            version = package.findtext("Version")
            archives = [a.strip() for a in (package.findtext("DownloadableArchives") or "").split(",") if a.strip()]
            for archive in archives:
                if archive.startswith(SKIPPED_ARCHIVES):
                    continue
                url = f"{folder}/{name}/{version}{archive}"
                print(f"downloading {name}: {archive}")
                data = fetch(url)
                expected = fetch(url + ".sha1").decode().split()[0].strip().lower()
                actual = hashlib.sha1(data).hexdigest()
                if actual != expected:
                    print(f"error: SHA-1 mismatch for {url}", file=sys.stderr)
                    return 1
                path = Path(temp) / archive
                path.write_bytes(data)
                subprocess.run(["7z", "x", "-y", f"-o{output}", str(path)], check=True, stdout=subprocess.DEVNULL)
                path.unlink()

    if not (output / "lib" / "cmake" / "Qt6").is_dir():
        print(f"error: no Qt kit found in {output}", file=sys.stderr)
        return 1
    print(output.as_posix())
    return 0


if __name__ == "__main__":
    sys.exit(main())
