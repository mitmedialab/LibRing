#!/bin/sh
# Copies the firmware binaries of every GitHub release next to the flasher web app:
#   <dir>/releases.json         the releases (tag, name, notes, date, .bin assets), newest first
#   <dir>/<tag>/<asset>.bin     their binaries
# A web page can't download them from GitHub itself (no CORS header on release downloads),
# so the deploy workflow runs this before publishing the site. Needs gh and jq (GH_TOKEN in CI).
#   apps/flasher/fetch-firmware.sh [dir]     default dir: apps/flasher/firmware
set -eu
REPO=${REPO:-mitmedialab/LibRing}
DIR=${1:-$(dirname "$0")/firmware}
mkdir -p "$DIR"
gh api "repos/$REPO/releases?per_page=100" --jq '[.[] | select(.draft | not) |
  {tag: .tag_name, name, body, published_at, prerelease, html_url,
   assets: [.assets[] | select(.name | endswith(".bin")) | {name, size}]}]' > "$DIR/releases.json"
for tag in $(jq -r '.[] | select(.assets | length > 0) | .tag' "$DIR/releases.json"); do
  mkdir -p "$DIR/$tag"
  gh release download "$tag" -R "$REPO" -p '*.bin' -D "$DIR/$tag" --clobber
done
echo "$(jq length "$DIR/releases.json") releases in $DIR"
