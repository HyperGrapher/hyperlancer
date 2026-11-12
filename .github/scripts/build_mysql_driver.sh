#!/usr/bin/env bash
# Builds Qt's MySQL driver (qsqlmysql.dll) for the Windows MinGW kit, because the official
# Qt 6.11 packages do not ship it. Uses the MariaDB client library from MSYS2 (preinstalled
# on GitHub's Windows runners). The plugin is installed into <kit>/plugins/sqldrivers, so
# windeployqt picks it up; libmariadb.dll and the DLLs it needs are copied to <runtime-dir>.
#
# Usage: build_mysql_driver.sh <qt-version> <kit-dir> <c++-compiler> <runtime-dir>
set -euo pipefail

qt_version="$1"
kit="$2"
compiler="$3"
runtime_dir="$4"
msys=/c/msys64
work="$(cygpath -m "$RUNNER_TEMP")/mysql-driver"
mkdir -p "$work" "$runtime_dir"

"$msys/usr/bin/pacman" -S --noconfirm --needed mingw-w64-x86_64-libmariadbclient > /dev/null

include_dir=""
for candidate in "$msys/mingw64/include/mysql" "$msys/mingw64/include/mariadb"; do
    if [ -f "$candidate/mysql.h" ]; then
        include_dir="$(cygpath -m "$candidate")"
        break
    fi
done
if [ -z "$include_dir" ]; then
    echo "mysql.h not found in MSYS2" >&2
    exit 1
fi
library="$(cygpath -m "$msys/mingw64/lib/libmariadb.dll.a")"

series="${qt_version%.*}"
curl -sSL --retry 4 -o "$work/qtbase.tar.xz" \
    "https://download.qt.io/official_releases/qt/$series/$qt_version/submodules/qtbase-everywhere-src-$qt_version.tar.xz"
# --force-local: GNU tar would read "D:/..." as a remote host:path.
tar --force-local -xf "$work/qtbase.tar.xz" -C "$work" "qtbase-everywhere-src-$qt_version/src/plugins/sqldrivers"

cmake -G Ninja \
    -S "$work/qtbase-everywhere-src-$qt_version/src/plugins/sqldrivers" \
    -B "$work/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$kit/lib/cmake/Qt6/qt.toolchain.cmake" \
    -DCMAKE_CXX_COMPILER="$compiler" \
    -DCMAKE_INSTALL_PREFIX="$kit" \
    -DFEATURE_sql_mysql=ON -DFEATURE_sql_odbc=OFF -DFEATURE_sql_psql=OFF -DFEATURE_sql_sqlite=OFF \
    -DFEATURE_sql_ibase=OFF -DFEATURE_sql_mimer=OFF -DFEATURE_sql_oci=OFF -DFEATURE_sql_db2=OFF \
    -DMySQL_INCLUDE_DIR="$include_dir" \
    -DMySQL_LIBRARY="$library"
cmake --build "$work/build" --parallel
cmake --install "$work/build"
test -f "$kit/plugins/sqldrivers/qsqlmysql.dll"

# libmariadb.dll and everything it loads from the MinGW tree (OpenSSL, zlib, ...).
cp "$msys/mingw64/bin/libmariadb.dll" "$runtime_dir/"
"$msys/usr/bin/ldd" "$msys/mingw64/bin/libmariadb.dll" | awk '$3 ~ /^\/mingw64\// { print $3 }' |
    while read -r dll; do
        cp "$msys$dll" "$runtime_dir/"
    done
echo "MySQL driver built; runtime DLLs:"
ls "$runtime_dir"
