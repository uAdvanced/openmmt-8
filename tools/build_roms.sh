#!/usr/bin/env bash
# Builds the OpenMMT-8 ROM, the OpenMMT-8 stock ROM, and the JAM-8 ROM, and copies them
# into dist/roms with a SHA256SUMS file. Each image is 32 KB.
set -euo pipefail

cd "$(dirname "$0")/.."

for tool in sdcc makebin python3; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "$tool is required. SDCC provides sdcc and makebin. Install SDCC and Python 3, then run this script again."
    exit 1
  fi
done

env -u OPENMMT8_CFLAGS OPENMMT8_PROFILE=default bash openmmt-8/tools/build_openmmt8_core.sh
env -u OPENMMT8_CFLAGS OPENMMT8_PROFILE=stock bash openmmt-8/tools/build_openmmt8_core.sh
bash jam8/tools/build_jam8.sh

out="dist/roms"
mkdir -p "$out"
cp -f openmmt-8/build/openmmt8_rom.bin openmmt-8/build/openmmt8_stock_rom.bin jam8/build/jam8_rom.bin "$out/"
(
  cd "$out"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum openmmt8_rom.bin openmmt8_stock_rom.bin jam8_rom.bin > SHA256SUMS
  else
    shasum -a 256 openmmt8_rom.bin openmmt8_stock_rom.bin jam8_rom.bin > SHA256SUMS
  fi
)

echo
echo "ROMs: $out/"
echo "  openmmt8_rom.bin        OpenMMT-8, part-edit extensions"
echo "  openmmt8_stock_rom.bin  OpenMMT-8 stock profile, with tape"
echo "  jam8_rom.bin            JAM-8 loop ROM"
