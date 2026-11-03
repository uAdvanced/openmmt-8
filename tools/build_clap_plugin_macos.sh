#!/usr/bin/env bash
# Builds the ROMs and the macOS CLAP plugin into dist/macos. Does not install the plugin.
# Run this script on macOS. The panel is part of this build and has not been tested on a Mac.
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "Run this script on macOS."
  exit 1
fi

platform="macos"
build_dir="emulator/build-macos"
cmake_extra=()
clap_folders=("~/Library/Audio/Plug-Ins/CLAP/" "/Library/Audio/Plug-Ins/CLAP/")
host_test() { "$build_dir/mmt8_clap_host_test" "$1"; }

source "$(dirname "$0")/clap_build_common.sh"
echo "The macOS panel has not been tested on a Mac."
