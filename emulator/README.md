# Emulator

This module runs the ROM images and builds the CLAP plugin.
The CPU is emu8051 with the patch in `patches/`.

CMake applies `patches/emu8051-mmt8-hardware-fidelity.patch` during configuration.
Do not commit the resulting change in `third_party/emu8051`.
The patch file is the record of that change.

## Build

Run these commands from the repository root.

```bash
cmake -S emulator -B emulator/build -DMMT8_EMU_ENABLE_CLAP=ON
cmake --build emulator/build -j
```

The release build of the plugin is `tools/build_clap_plugin_<platform>.sh`.
Read `README.md` in the repository root.

## Programs

- `mmt8_clap_plugin.clap`: the plugin.
- `mmt8_clap_host_test`: loads the plugin the way a host does.
- `mmt8_clap_engine_test`: drives the plugin engine with the built-in ROMs.
- `mmt8_clap_wrapper_mmio_probe`: runs a ROM headless with key presses, MIDI in, and
  holds, and prints the LCD, the LEDs, and MIDI out. The probes in
  `openmmt-8/tests/` and `jam8/tests/` drive it.

## Layout

- `core/`: machine and ROM image interfaces.
- `frontends/clap/`: the CLAP plugin and the headless wrapper. Read `frontends/clap/README.md`.
- `patches/`: the emu8051 patch.
- `third_party/`: emu8051 and Dear ImGui (submodules), and the CLAP headers.
