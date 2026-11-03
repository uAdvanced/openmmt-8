# Third-party notices

The MIT license in `LICENSE` covers the OpenMMT-8 and JAM-8 code in this repository.
The components below keep their own licenses.
The full text of each of those licenses is in `LICENSES/`.
Keep this file and `LICENSES/` with the plugin when you distribute the plugin.

## Included, with permission

These components allow redistribution.
Keep the copyright lines with every copy.

### emu8051

Copyright (c) 2022 Jari Komppa

MIT License. The full text is `LICENSES/emu8051-MIT.txt`.
The source is the submodule `emulator/third_party/emu8051`.
The plugin links this code.
CMake applies `emulator/patches/emu8051-mmt8-hardware-fidelity.patch` at configure time.

### Dear ImGui

Copyright (c) 2014-2026 Omar Cornut

MIT License. The full text is `LICENSES/imgui-MIT.txt`.
The source is the submodule `emulator/third_party/imgui`.
The Windows plugin and the Linux plugin link this code.

### CLAP headers

Copyright (c) 2021 Alexandre Bique

MIT License. The full text is `LICENSES/clap-MIT.txt`.
The headers are in `emulator/third_party/clap`.

### Roboto Medium

Copyright 2015 Google Inc. All Rights Reserved.

The panel uses this font.
The file is `emulator/third_party/imgui/misc/fonts/Roboto-Medium.ttf`.
The plugin embeds the font.
The font is under the Apache License, Version 2.0.
The full text is `LICENSES/Apache-2.0.txt`.
The font notice is `LICENSES/Roboto-NOTICE.txt`.

## Not included

Do not add these files to a release.
This repository does not give you the right to distribute them.

### Original unit ROM

The plugin does not contain the original unit's ROM.
Load a ROM file only when you have the right to use that file.

### Original unit manual

This repository does not contain the original unit's manual.
Use your own copy of the unit manual for the original functions.

### SDCC

The ROM build uses SDCC.
SDCC is a separate program.
This repository does not include SDCC.
Install SDCC from its own project before you build the ROM.
