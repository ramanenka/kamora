#!/bin/sh
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
topdir=${RPM_TOPDIR:-$HOME/rpmbuild}
match='v[0-9]*.[0-9]*'

git() { command git -c safe.directory="$root" -C "$root" "$@"; }

usage() {
    echo "usage: $0 [srpm OUTDIR]" >&2
    exit 2
}

case ${1-} in
    "") mode=rpm ;;
    srpm) [ $# -eq 2 ] || usage; mode=srpm; outdir=$2 ;;
    *) usage ;;
esac

if tag=$(git describe --tags --exact-match --match "$match" HEAD 2>/dev/null); then
    version=${tag#v}
    previous=$(git describe --tags --abbrev=0 --match "$match" HEAD^ 2>/dev/null || true)
elif [ "$mode" = srpm ]; then
    echo "HEAD is not on a release tag, refusing to build a release" >&2
    exit 1
elif described=$(git describe --tags --long --match "$match" HEAD 2>/dev/null); then
    version=$(echo "$described" | sed -E 's/^v//; s/-([0-9]+)-g/^\1.g/')
else
    version=0^g$(git rev-parse --short HEAD)
fi

stamp=$(git log -1 --format=%ct HEAD)
author=$(git log -1 --format='%an <%ae>' HEAD)

if [ -n "${tag-}" ] && [ -n "$previous" ]; then
    changes=$(git log --no-merges --reverse --format='- %s' "$previous..HEAD" | sed 's/%/%%/g')
elif [ -n "${tag-}" ]; then
    changes="- First release"
else
    changes="- Snapshot of $(git rev-parse --short HEAD)"
fi

mkdir -p "$topdir/SOURCES" "$topdir/SPECS"
git archive --format=tar.gz \
    --prefix="kamora-$version/" \
    -o "$topdir/SOURCES/kamora-$version.tar.gz" HEAD

spec=$topdir/SPECS/kamora.spec
{
    sed "s/^Version:.*/Version:        $version/" "$here/kamora.spec"
    echo "* $(LC_ALL=C date -u -d "@$stamp" '+%a %b %d %Y') $author - $version-1"
    echo "$changes"
} > "$spec"

if [ "$mode" = srpm ]; then
    rpmbuild --define "_topdir $topdir" --define "_srcrpmdir $outdir" --nodeps -bs "$spec"
else
    rpmbuild --define "_topdir $topdir" -bb "$spec"
fi
