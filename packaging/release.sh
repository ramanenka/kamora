#!/bin/sh
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
git() { command git -C "$root" "$@"; }

if [ -n "$(git status --porcelain)" ]; then
    echo "The working tree has uncommitted changes" >&2
    exit 1
fi

git fetch --quiet --tags origin

if ! git merge-base --is-ancestor HEAD origin/main; then
    echo "HEAD is not on origin/main; merge and push it first" >&2
    exit 1
fi

if current=$(git describe --tags --exact-match --match 'v[0-9]*.[0-9]*' HEAD 2>/dev/null); then
    echo "HEAD is already released as $current" >&2
    exit 1
fi

latest=$(git tag --list 'v*' | grep -E '^v[0-9]+\.[0-9]+$' | sort -V | tail -n 1 || true)
if [ -z "$latest" ]; then
    next=v0.1
else
    major=$(echo "$latest" | sed -E 's/^v([0-9]+)\..*/\1/')
    minor=$(echo "$latest" | sed -E 's/^v[0-9]+\.([0-9]+)$/\1/')
    next=v$major.$((minor + 1))
fi

echo "Latest release: ${latest:-none}"
echo "Tag $(git log -1 --format='%h %s' HEAD) as $next and push it? [y/N] "
read -r answer
[ "$answer" = y ] || [ "$answer" = Y ] || exit 1

git tag --annotate "$next" --message "Kamora ${next#v}"
git push origin "$next"
echo "Pushed $next; COPR builds it at https://copr.fedorainfracloud.org/coprs/ramanenka/kamora/builds/"
