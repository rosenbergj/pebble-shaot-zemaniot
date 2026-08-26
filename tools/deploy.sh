#!/usr/bin/env bash
#
# Stage a build in dist/ for deployment, and promote one to last-known-good.
#
#   tools/deploy.sh          build the current commit into dist/
#   tools/deploy.sh --good   mark what is in dist/ as known good
#
# dist/ is scp'd to a file share and installed from a phone. Two files here are
# the main development path, and this script owns both of them:
#
#   pt2-shaot-watchface.pbw           the latest build -- install this one
#   pt2-shaot-watchface-lastgood.pbw  the rollback -- install this if that fails
#
# They share a UUID, which is what makes rollback a single install. Anything
# else currently useful -- a diagnostic probe, a comparison build -- may sit
# here too, as long as it has its own UUID and its own displayName so a phone
# can tell them apart. Delete those when they stop being useful; the directory
# should not accumulate.
#
# BUILD.txt lists whatever else it finds, so that manifest cannot drift from
# what is actually in the directory.
#
# dist/ is gitignored, so nothing here can be restored by git. The rollback is
# never written except through --good.

set -euo pipefail

cd "$(dirname "$0")/.."

DIST=dist
PBW="$DIST/pt2-shaot-watchface.pbw"
LASTGOOD="$DIST/pt2-shaot-watchface-lastgood.pbw"
BUILDINFO="$DIST/BUILD.txt"

version() { python3 -c 'import json; print(json.load(open("package.json"))["version"])'; }

# The version deployed last, or empty if nothing has been.
deployed_version() {
  [ -f "$BUILDINFO" ] && sed -n 's/^version: //p' "$BUILDINFO" || true
}

deployed_sha() {
  [ -f "$BUILDINFO" ] && sed -n 's/^commit: //p' "$BUILDINFO" || true
}

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
  extras=$(find "$DIST" -maxdepth 1 -name '*.pbw' \
             ! -name "$(basename "$PBW")" ! -name "$(basename "$LASTGOOD")" | sort)
  if [ -n "$extras" ]; then
    echo "Also here, and not part of the main build:"
    echo "$extras" | while read -r f; do
      # Read the name out of the bundle rather than trusting the filename, since
      # that is what the phone will show in its list.
      label=$(unzip -p "$f" appinfo.json 2>/dev/null |
              python3 -c 'import json,sys
try:
    d = json.load(sys.stdin)
    print("%s %s" % (d.get("longName", "?"), d.get("versionLabel", "")))
except Exception:
    print("")' 2>/dev/null)
      echo "  $(basename "$f")  --  ${label:-unknown bundle}"
    done
    echo
  fi
  if [ -f "$LASTGOOD" ]; then
    lastgood=$(git tag -l 'good-*' --sort=-v:refname | head -1)
    echo "If it misbehaves, install pt2-shaot-watchface-lastgood.pbw, which is"
    echo "${lastgood:-an earlier build}."
  else
    echo "There is no last-known-good build yet, so there is nothing to roll"
    echo "back to. Confirm this one on the watch and run tools/deploy.sh --good."
  fi
} >"$BUILDINFO"

echo "Staged $v ($(git rev-parse --short HEAD)) in $DIST."
