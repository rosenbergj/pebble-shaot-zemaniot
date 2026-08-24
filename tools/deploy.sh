#!/usr/bin/env bash
#
# Stage a build in dist/ for deployment, and promote one to last-known-good.
#
#   tools/deploy.sh          build the current commit into dist/
#   tools/deploy.sh --good   mark what is in dist/ as known good
#
# dist/ is scp'd to a file share and installed from a phone, so it holds only
# three files and each of them means something:
#
#   pt2-shaot-watchface.pbw           the latest build -- install this one
#   pt2-shaot-watchface-lastgood.pbw  the rollback -- install this if that fails
#   pt2-shaot-watchface-phase4-js.pbw the pre-port JavaScript build
#
# dist/ is gitignored, so nothing here can be restored by git. The script never
# writes to the two older files except through --good, and it verifies the
# JavaScript build is intact on every run.

set -euo pipefail

cd "$(dirname "$0")/.."

DIST=dist
PBW="$DIST/pt2-shaot-watchface.pbw"
LASTGOOD="$DIST/pt2-shaot-watchface-lastgood.pbw"
BUILDINFO="$DIST/BUILD.txt"

# The pre-port build is the only thing here that cannot be rebuilt from a commit:
# it came from a tree that no longer builds. Checked on every run so a mistake
# surfaces now rather than when it is needed.
JS_PBW="$DIST/pt2-shaot-watchface-phase4-js.pbw"
JS_SHA=4712f5bf7cef90559fee912303e4c14b5008abeb3c4dbe081accd1dbbc360489

check_js_build() {
  if [ ! -f "$JS_PBW" ]; then
    echo "FATAL: $JS_PBW is missing. It cannot be rebuilt; recover it before continuing." >&2
    exit 1
  fi
  local have
  have=$(sha256sum "$JS_PBW" | cut -d' ' -f1)
  if [ "$have" != "$JS_SHA" ]; then
    echo "FATAL: $JS_PBW has changed. Expected $JS_SHA, got $have." >&2
    exit 1
  fi
}

version() { python3 -c 'import json; print(json.load(open("package.json"))["version"])'; }

# The version deployed last, or empty if nothing has been.
deployed_version() {
  [ -f "$BUILDINFO" ] && sed -n 's/^version: //p' "$BUILDINFO" || true
}

deployed_sha() {
  [ -f "$BUILDINFO" ] && sed -n 's/^commit: //p' "$BUILDINFO" || true
}

check_js_build

# --- promote ----------------------------------------------------------------

if [ "${1:-}" = "--good" ]; then
  v=$(deployed_version)
  sha=$(deployed_sha)
  if [ -z "$v" ] || [ ! -f "$PBW" ]; then
    echo "Nothing staged in $DIST to promote." >&2
    exit 1
  fi
  cp "$PBW" "$LASTGOOD"
  # An immutable tag per confirmed build, rather than one moving tag: after a
  # regression the useful question is what changed since the last good one, and
  # `git diff good-<version>..HEAD` answers it directly.
  if git rev-parse -q --verify "refs/tags/good-$v" >/dev/null; then
    echo "Tag good-$v already exists; leaving it alone."
  else
    git tag -a "good-$v" "$sha" -m "confirmed working on hardware"
  fi
  # BUILD.txt names the rollback file's version, and the promotion just changed
  # which build that file holds -- so the line has to move with it. Only the
  # build path used to write this file, which left it naming the *previous*
  # good build after every promotion. It is read on a phone at the moment a
  # rollback is wanted, where a stale version number reinstalls the build being
  # rolled back from.
  if [ -f "$BUILDINFO" ]; then
    # All three variants are deleted, including the one written just below, so
    # that promoting twice rewrites rather than repeats.
    sed -i -e '/^If it misbehaves, install/,+1d' \
           -e '/^There is no last-known-good build yet;/,+1d' \
           -e '/^This build is also the last-known-good one/,+3d' "$BUILDINFO"
    {
      echo "This build is also the last-known-good one (good-$v), so"
      echo "pt2-shaot-watchface-lastgood.pbw is a copy of it. The next build"
      echo "stages over pt2-shaot-watchface.pbw and leaves this one to roll"
      echo "back to."
    } >>"$BUILDINFO"
  fi
  echo "$v is now last-known-good ($LASTGOOD, tag good-$v)."
  exit 0
fi

# --- build ------------------------------------------------------------------

if [ -n "$(git status --porcelain)" ]; then
  echo "Working tree is dirty. Commit first: a staged build has to name the" >&2
  echo "commit it came from, or the rollback tag points at the wrong tree." >&2
  exit 1
fi

v=$(version)
prev=$(deployed_version)
if [ "$v" = "$prev" ]; then
  # A stable filename means the phone cannot show that a new file arrived. The
  # version can, so it has to move every time.
  echo "package.json is still at $v, which is already deployed." >&2
  echo "Bump the version and amend the commit." >&2
  exit 1
fi

# pebble build has been seen to fail while a later install silently pushes the
# previous .pbw. Removing the output first means a failure cannot masquerade as
# a success here.
rm -f build/pt2-shaot-watchface.pbw
pebble build
if [ ! -f build/pt2-shaot-watchface.pbw ]; then
  echo "Build produced no .pbw." >&2
  exit 1
fi

mkdir -p "$DIST"
cp build/pt2-shaot-watchface.pbw "$PBW"

sha=$(git rev-parse HEAD)
{
  echo "version: $v"
  echo "commit: $sha"
  echo "built: $(date -Iseconds)"
  echo "subject: $(git log -1 --format=%s)"
  echo
  echo "Install pt2-shaot-watchface.pbw. The phone shows the version, so check"
  echo "it reads $v -- that is how you know the new file arrived and installed."
  echo
  if [ -f "$LASTGOOD" ]; then
    lastgood=$(git tag -l 'good-*' --sort=-v:refname | head -1)
    echo "If it misbehaves, install pt2-shaot-watchface-lastgood.pbw, which is"
    echo "${lastgood:-an earlier build}."
  else
    echo "There is no last-known-good build yet; pt2-shaot-watchface-phase4-js.pbw"
    echo "is the only fallback, and it is the pre-port JavaScript face."
  fi
} >"$BUILDINFO"

echo "Staged $v ($(git rev-parse --short HEAD)) in $DIST."
