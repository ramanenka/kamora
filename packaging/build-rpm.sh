#!/bin/sh
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
version=$(sed -n 's/^Version: *//p' "$here/kamora.spec")
topdir=${RPM_TOPDIR:-$HOME/rpmbuild}

mkdir -p "$topdir/SOURCES"
git -C "$root" archive --format=tar.gz \
    --prefix="kamora-$version/" \
    -o "$topdir/SOURCES/kamora-$version.tar.gz" HEAD

rpmbuild --define "_topdir $topdir" -bb "$here/kamora.spec"
