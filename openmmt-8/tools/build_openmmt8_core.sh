#!/usr/bin/env bash
# Build the OpenMMT-8 core firmware (C only, SDCC) into a 32 KB EPROM image.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="${ROOT_DIR}/core"
# Feature profile (see core.h build options):
#   default  the part EDIT extensions; the TAPE key holds only the MIDI dump page
#   stock    the original unit's features: tape on the TAPE key, part EDIT as it was
case "${OPENMMT8_PROFILE:=default}" in
  default)
    PROFILE_DEFS="-DOPENMMT8_EDITX"
    BUILD_DIR="${ROOT_DIR}/build/core"
    ROM="${ROOT_DIR}/build/openmmt8_core.bin"
    ROM_COPY="${ROOT_DIR}/build/openmmt8_rom.bin"
    ;;
  stock)
    PROFILE_DEFS="-DOPENMMT8_TAPE"
    BUILD_DIR="${ROOT_DIR}/build/core-stock"
    ROM="${ROOT_DIR}/build/openmmt8_stock_core.bin"
    ROM_COPY="${ROOT_DIR}/build/openmmt8_stock_rom.bin"
    ;;
  *)
    echo "error: OPENMMT8_PROFILE must be default or stock (got ${OPENMMT8_PROFILE})"; exit 1 ;;
esac
# gen_screens.py reads the defines from here too.
export OPENMMT8_CFLAGS="${PROFILE_DEFS} ${OPENMMT8_CFLAGS:-}"
IHX="${BUILD_DIR}/openmmt8_core.ihx"
ROM_BYTES=${ROM_BYTES:-32768}
# Relocatable xdata lives between the MIDI rings (0x0000-0x01FF) and the part table (0x0400).
XRAM_LOC=0x0200
XRAM_SIZE=0x0200

command -v sdcc >/dev/null || { echo "error: sdcc not found"; exit 1; }
command -v makebin >/dev/null || { echo "error: makebin not found"; exit 1; }
mkdir -p "${BUILD_DIR}"
python3 "${GEN_SCREENS:-${ROOT_DIR}/tools/gen_screens.py}" "${SRC_DIR}/screens.def" "${BUILD_DIR}/screens.h"

FLAGS=(-mmcs51 --model-small --opt-code-size --less-pedantic --noinduction --fomit-frame-pointer --no-xinit-opt
       --iram-size 128 --stack-size ${STACK_SIZE:-0x1f} --code-loc 0x0000 --code-size "${ROM_BYTES}"
       --xram-loc "${XRAM_LOC}" --xram-size "${XRAM_SIZE}" -I "${SRC_DIR}" -I "${BUILD_DIR}")
# Extra defines for experiments, e.g. OPENMMT8_CFLAGS="-DT0_PHASE=100".
read -r -a EXTRA <<< "${OPENMMT8_CFLAGS:-}"
FLAGS+=("${EXTRA[@]}")

RELS=()
for src in "${SRC_DIR}"/*.c; do
  asm="${BUILD_DIR}/$(basename "${src}" .c).asm"
  peep=(--peep-asm)
  [[ "$(basename "${src}")" == ui.c ]] && peep=()
  sdcc "${FLAGS[@]}" "${peep[@]}" -S -o "${asm}" "${src}"
  RELS+=("${asm%.asm}.rel")
done
python3 "${ROOT_DIR}/tools/prune_saves.py" "${BUILD_DIR}" ui seq
python3 "${ROOT_DIR}/tools/outline_asm.py" "${BUILD_DIR}" ui seq
# main.rel first so the reset/interrupt vectors come from its module; lcd next to ui, its
# caller, so more of those calls stay in the page.
RELS=()
for m in ${LINK_ORDER:-main keys midi seq lcd ui}; do RELS+=("${BUILD_DIR}/${m}.rel"); done
assemble() {
  for rel in "${RELS[@]}"; do sdas8051 -plosgffw "${rel}" "${rel%.rel}.asm"; done
}
link() {
  assemble
  # Linked without the EPROM limit: the image is checked below, after calls are shortened.
  rm -f "${IHX}"
  sdcc "${FLAGS[@]}" --code-size 0x10000 --out-fmt-ihx -o "${IHX}" "${RELS[@]}" 2>&1 | tee "${BUILD_DIR}/link.log" || true
  if ! [ -f "${IHX}" ]; then echo "error: link produced no ${IHX}"; exit 1; fi
  if grep -q -e "undefined symbol" -e "ASlink-Error" "${BUILD_DIR}/link.log"; then
    echo "error: link failed"; exit 1
  fi
}
# Relink until same-page acall/ajmp shortening is stable (layout shifts expose more sites).
relax() {
  for _ in $(seq 1 20); do
    n=$(python3 "${ROOT_DIR}/tools/relax_calls.py" "${BUILD_DIR}" 2>/dev/null || echo 0)
    [ "${n}" = 0 ] && break
    link
  done
}
# Then order the functions for more same-page calls, from the shortened layout, and again.
REORDER=(${REORDER_MODS:-ui seq})
for m in "${REORDER[@]}"; do cp "${BUILD_DIR}/${m}.asm" "${BUILD_DIR}/${m}.asm.pre"; done
link
relax
for _ in $(seq 1 "${REORDER_ROUNDS:-3}"); do
  python3 "${ROOT_DIR}/tools/reorder_asm.py" "${BUILD_DIR}" "${REORDER[@]}"
  link
  relax
done
if grep -q "2K Page relocation" "${BUILD_DIR}/link.log"; then
  echo "error: a short call or jump left its 2 KB page"; exit 1
fi

python3 - "${IHX}" "${BUILD_DIR}/openmmt8_core.map" "${ROM_BYTES}" "${STACK_SIZE:-0x1f}" <<'PY'
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
    if start < 0x200 or start + size > 0x400:
        sys.exit(f"error: XSEG 0x{start:04x}+0x{size:x} leaves 0x0200-0x03FF")
m = re.search(r"^\s*[0-9A-F]+\s+__start__stack", text, re.M)
stack = int(m.group(0).split()[0], 16) if m else 0
if stack + stack_sz > 0x80:
    sys.exit(f"error: stack at 0x{stack:02x} + 0x{stack_sz:x} passes the 8031's 128 bytes of internal RAM")
print(f"OpenMMT-8 core: {used} of {limit} bytes used ({limit - used} free), stack at 0x{stack:02x}")
PY
makebin -s "${ROM_BYTES}" "${IHX}" "${ROM}"
cp "${ROM}" "${ROM_COPY}"
echo "  rom (${OPENMMT8_PROFILE}): ${ROM} (also ${ROM_COPY#"${ROOT_DIR}/"})"
