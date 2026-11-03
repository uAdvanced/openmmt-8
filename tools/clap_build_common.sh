# Shared steps for tools/build_clap_plugin_linux.sh, _macos.sh and _windows.sh.
# The caller sets: platform, build_dir, cmake_extra (array), host_test (function), and
# clap_folders (array of folders that hosts scan). This file builds the ROMs and the plugin,
# loads the plugin once the way a host does, and stages a release folder in dist/<platform>.
# It does not copy anything into a host folder.

cd "$(dirname "${BASH_SOURCE[0]}")/.."

bash tools/build_roms.sh

cmake -S emulator -B "$build_dir" \
  -DMMT8_EMU_ENABLE_CLAP=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DMMT8_BUILTIN_ROM="$(pwd)/openmmt-8/build/openmmt8_rom.bin" \
  -DMMT8_JAM_ROM="$(pwd)/jam8/build/jam8_rom.bin" \
  ${cmake_extra[@]+"${cmake_extra[@]}"}
cmake --build "$build_dir" -j --target mmt8_clap_plugin mmt8_clap_host_test

plugin="$build_dir/mmt8_clap_plugin.clap"
if [[ ! -f "$plugin" ]]; then
  echo "missing plugin file: $plugin"
  exit 1
fi

host_test "$plugin"

dist="dist/$platform"
rm -rf "$dist"
mkdir -p "$dist/roms"
cp -f "$plugin" "$dist/"
cp -f dist/roms/*.bin dist/roms/SHA256SUMS "$dist/roms/"
bash tools/stage_clap_docs.sh "$dist"

echo
echo "Release folder: $dist/"
echo "Plugin file: $dist/mmt8_clap_plugin.clap"
echo "ROM files: $dist/roms/"
echo "Copy the plugin file into one of these folders:"
for folder in "${clap_folders[@]}"; do echo "  $folder"; done
echo "Keep LICENSE, THIRD_PARTY_NOTICES.md, and LICENSES/ with the plugin file."
