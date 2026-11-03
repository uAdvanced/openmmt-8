#!/usr/bin/env bash
# Build the JAM-8 ROM (C, SDCC) into a 32 KB image.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="${ROOT_DIR}/core"
BUILD_DIR="${ROOT_DIR}/build/core"
IHX="${BUILD_DIR}/jam8.ihx"
ROM="${ROOT_DIR}/build/jam8_rom.bin"
ROM_BYTES=${ROM_BYTES:-32768}
STACK_SIZE=${STACK_SIZE:-0x28}
# Compiler work area: between the MIDI rings (0x0000-0x01FF) and the settings (0x0800).
XRAM_LOC=0x0200
XRAM_SIZE=0x0600

command -v sdcc >/dev/null || { echo "error: sdcc not found"; exit 1; }
command -v makebin >/dev/null || { echo "error: makebin not found"; exit 1; }
mkdir -p "${BUILD_DIR}"

FLAGS=(-mmcs51 --model-small --opt-code-size --less-pedantic --fomit-frame-pointer
       --iram-size 128 --stack-size "${STACK_SIZE}" --code-loc 0x0000 --code-size "${ROM_BYTES}"
       --xram-loc "${XRAM_LOC}" --xram-size "${XRAM_SIZE}" -I "${SRC_DIR}")
read -r -a EXTRA <<< "${JAM8_CFLAGS:-}"
FLAGS+=("${EXTRA[@]}")

RELS=()
# main first so the reset and interrupt vectors come from its module.
for m in main midi keys lcd clock engine gen harmony scene tables ui; do
  sdcc "${FLAGS[@]}" -c -o "${BUILD_DIR}/${m}.rel" "${SRC_DIR}/${m}.c"
  RELS+=("${BUILD_DIR}/${m}.rel")
done
rm -f "${IHX}"
sdcc "${FLAGS[@]}" --out-fmt-ihx -o "${IHX}" "${RELS[@]}" > "${BUILD_DIR}/link.log" 2>&1 || true
cat "${BUILD_DIR}/link.log"
if ! [ -f "${IHX}" ] || grep -q -e "undefined symbol" -e "ASlink-Error" "${BUILD_DIR}/link.log"; then
  echo "error: link failed"; exit 1
fi

python3 - "${IHX}" "${BUILD_DIR}/jam8.map" "${ROM_BYTES}" "${STACK_SIZE}" <<'PY'
import re, sys
ihx, map_path, limit, stack_sz = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4], 16)
used = 0
for line in open(ihx):
    if line.startswith(":") and line[7:9] == "00":
        used = max(used, int(line[3:7], 16) + int(line[1:3], 16))
if used > limit:
    sys.exit(f"error: image uses {used} bytes, EPROM holds {limit}")
text = open(map_path).read()
m = re.search(r"^XSEG\s+([0-9A-F]{8})\s+([0-9A-F]{8})", text, re.M)
if m:
    start, size = int(m.group(1), 16), int(m.group(2), 16)
    if start < 0x200 or start + size > 0x800:
        sys.exit(f"error: XSEG 0x{start:04x}+0x{size:x} leaves 0x0200-0x07FF")
m = re.search(r"^\s*[0-9A-F]+\s+__start__stack", text, re.M)
stack = int(m.group(0).split()[0], 16) if m else 0
if stack + stack_sz > 0x80:
    sys.exit(f"error: stack at 0x{stack:02x} + 0x{stack_sz:x} passes the 8031's 128 bytes of internal RAM")
print(f"JAM-8: {used} of {limit} bytes used ({limit - used} free), stack at 0x{stack:02x}")
PY
makebin -s "${ROM_BYTES}" "${IHX}" "${ROM}"
echo "  rom: ${ROM}"
