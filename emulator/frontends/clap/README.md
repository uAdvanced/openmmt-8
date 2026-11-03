# OpenMMT-8 CLAP plugin

The plugin runs the unit from its ROM.
The default built-in ROM is OpenMMT-8.
The second built-in ROM is JAM-8, a loop and pattern ROM.
The ROM menu has "Use built-in JAM-8".
Read `README.md` and `MANUAL.md` in the repository root.
Read `jam8/MANUAL.md` for JAM-8.

## Build

```bash
bash tools/build_clap_plugin_linux.sh     # dist/linux/
bash tools/build_clap_plugin_macos.sh     # dist/macos/, on a Mac
bash tools/build_clap_plugin_windows.sh   # dist/windows/, from Linux or WSL with mingw-w64
```

Each script builds the ROMs and the plugin, and writes a release folder with the plugin,
the ROMs, the manuals, and the license files.
No script copies the plugin into a host folder.

## Panel

The panel window is in the Windows, Linux, and macOS builds.
The Linux panel uses X11.
The host must provide an X11 window.
The macOS panel has not been tested on a Mac.

The display, the lamps, and the keys follow the ROM.
Read `MANUAL.md` for the mouse, the computer keys, DAW sync, the click level, and the ROM menu.

## Files

- `plugin/clap_plugin.cpp`: CLAP entry, factory, and extensions.
- `plugin/engine.*`: one unit. The audio thread runs the emulator.
- `plugin/project_state.*`: the project data.
- `plugin/panel_ui.*`: panel drawing. Windows, Linux, and macOS share this code.
- `plugin/gui_win32.cpp`: Windows window.
- `plugin/gui_x11.cpp`: Linux window.
- `plugin/gui_cocoa.mm`: macOS window. This file has not been built or run on a Mac.
- `plugin/lcd_font.h`: character patterns for the display.
- `third_party/clap`: CLAP headers.

## Tests

```bash
cmake -S emulator -B emulator/build -DMMT8_EMU_ENABLE_CLAP=ON && cmake --build emulator/build -j
emulator/build/mmt8_clap_host_test emulator/build/mmt8_clap_plugin.clap
emulator/build/mmt8_clap_engine_test dist/roms/openmmt8_stock_rom.bin
```

The engine test takes an optional ROM file to load from the ROM menu.
Without it, the test skips the loaded-ROM checks.
