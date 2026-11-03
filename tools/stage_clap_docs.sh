#!/usr/bin/env bash
# Copies the files that travel with a plugin build. Does not install the plugin.
set -euo pipefail

dest="${1:?destination directory}"
root="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$dest"
cp -f "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.md" "$root/README.md" "$root/MANUAL.md" "$dest/"
cp -f "$root/jam8/MANUAL.md" "$dest/JAM8_MANUAL.md"
rm -rf "$dest/LICENSES"
cp -a "$root/LICENSES" "$dest/LICENSES"
echo "docs staged: $dest"
