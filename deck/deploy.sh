#!/usr/bin/env bash
# Copies the built AppImage to the Steam Deck over ssh (deck@steamdeck, key
# login), only while the game is not running, and checks it arrived whole.
# See docs/STEAM_DECK.md.
#
#   deck/deploy.sh           fails if the game is running
#   deck/deploy.sh --wait    waits (up to an hour) for the game to exit first
#   APPIMAGE=<file> deck/deploy.sh   deploys that build instead (the PGO profile build)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOST="${DECK_HOST:-deck@steamdeck}"
DEST="${DECK_DIR:-/run/media/deck/SM256/Recompilations/reNut}"
APPIMAGE="${APPIMAGE:-$ROOT/out/build/linux-amd64-release/renut-x86_64.AppImage}"

running() { ssh -o BatchMode=yes "$HOST" 'pgrep -x AppRun >/dev/null'; }

if [ "${1:-}" = "--wait" ]; then
  for _ in $(seq 720); do running || break; sleep 5; done
fi
if running; then
  echo "reNut is running on the Deck: stop it first (or use --wait)" >&2
  exit 1
fi

scp -q "$APPIMAGE" "$HOST:$DEST/renut.AppImage.new"
remote="$(ssh -o BatchMode=yes "$HOST" "sha256sum '$DEST/renut.AppImage.new'" | cut -d' ' -f1)"
local_sum="$(sha256sum "$APPIMAGE" | cut -d' ' -f1)"
if [ "$remote" != "$local_sum" ]; then
  echo "checksum mismatch ($remote vs $local_sum); the old build is still in place" >&2
  exit 1
fi
ssh -o BatchMode=yes "$HOST" "chmod +x '$DEST/renut.AppImage.new' && mv -f '$DEST/renut.AppImage.new' '$DEST/renut.AppImage'"
echo "deployed $local_sum"
