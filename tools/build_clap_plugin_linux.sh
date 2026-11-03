#!/usr/bin/env bash
# Builds the ROMs and the Linux CLAP plugin into dist/linux. Does not install the plugin.
set -euo pipefail

platform="linux"
build_dir="emulator/build-linux"
cmake_extra=()
clap_folders=("~/.clap/" "/usr/local/lib/clap/" "/usr/lib/clap/")
host_test() { "$build_dir/mmt8_clap_host_test" "$1"; }

source "$(dirname "$0")/clap_build_common.sh"
echo "The Linux panel uses X11. The host must provide an X11 window."
