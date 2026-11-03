#pragma once

#include <cstddef>

namespace mmt8 {

/* OpenMMT-8 ROM image embedded at build time (cmake/embed_binary.cmake); size 0 if the build had none. */
extern const unsigned char kBuiltinRom[];
extern const size_t kBuiltinRomSize;

/* JAM-8 loop ROM image (jam8/tools/build_jam8.sh), embedded the same way. */
extern const unsigned char kJamRom[];
extern const size_t kJamRomSize;

/* Roboto Medium (Apache 2.0, from Dear ImGui misc/fonts) for the panel legends. */
extern const unsigned char kPanelFont[];
extern const size_t kPanelFontSize;

}  // namespace mmt8
