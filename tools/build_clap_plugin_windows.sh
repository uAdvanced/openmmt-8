#!/usr/bin/env bash
# Builds the ROMs and the Windows CLAP plugin into dist/windows. Does not install the plugin.
# Run this script on Linux or WSL with the mingw-w64 cross compiler (x86_64-w64-mingw32-g++).
set -euo pipefail

if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
  echo "x86_64-w64-mingw32-g++ is required. Install mingw-w64, then run this script again."
  exit 1
fi

platform="windows"
build_dir="emulator/build-win64"
cmake_extra=(-DCMAKE_TOOLCHAIN_FILE="$(cd "$(dirname "$0")/.." && pwd)/emulator/cmake/toolchains/mingw64.cmake")
clap_folders=('%LOCALAPPDATA%\Programs\Common\CLAP\' '%COMMONPROGRAMFILES%\CLAP\')
# The host test is a Windows program. WSL can start it; a plain Linux machine cannot.
host_test() {
  if command -v wslpath >/dev/null 2>&1; then
    "$build_dir/mmt8_clap_host_test.exe" "$(wslpath -w "$1")"
  else
    echo "host test skipped: run $build_dir/mmt8_clap_host_test.exe on Windows to load the plugin once"
  fi
}

source "$(dirname "$0")/clap_build_common.sh"
