cmake_minimum_required(VERSION 3.20)
project(mmt8_emulator LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MMT8_EMU_ENABLE_CLAP "Enable CLAP plugin wrapper targets" OFF)

# The upstream emu8051 submodule needs local CPU/peripheral fixes (MOVX @Ri paging, AC flag,
# MOV direct,@Ri operand order, Timer1 baud clock, level-triggered serial interrupt,
# multi-cycle instruction timing).
set(MMT8_EMU8051_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/emu8051")
set(MMT8_EMU8051_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/patches/emu8051-mmt8-hardware-fidelity.patch")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${MMT8_EMU8051_DIR}/core.c")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${MMT8_EMU8051_PATCH}"
    WORKING_DIRECTORY "${MMT8_EMU8051_DIR}"
    RESULT_VARIABLE mmt8_emu8051_patched
    OUTPUT_QUIET ERROR_QUIET)
  if(NOT mmt8_emu8051_patched EQUAL 0)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" apply "${MMT8_EMU8051_PATCH}"
      WORKING_DIRECTORY "${MMT8_EMU8051_DIR}"
      RESULT_VARIABLE mmt8_emu8051_apply)
    if(NOT mmt8_emu8051_apply EQUAL 0)
      message(FATAL_ERROR "failed to apply ${MMT8_EMU8051_PATCH}")
    endif()
  endif()
endif()

add_library(
  mmt8_emu_core
  core/src/rom_image.cpp
  core/src/trace_sink.cpp
  core/src/machine.cpp
  core/src/script_engine.cpp
)
set_target_properties(mmt8_emu_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(mmt8_emu_core PUBLIC core/include)
target_compile_options(mmt8_emu_core PRIVATE -Wall -Wextra -Wpedantic)

# Development programs whose source is absent (the public tree leaves some out) are skipped.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/frontends/linux_cli/main.cpp")
  add_executable(
    mmt8_emu_cli
    frontends/linux_cli/main.cpp
  )
  target_link_libraries(mmt8_emu_cli PRIVATE mmt8_emu_core)
  target_compile_options(mmt8_emu_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MMT8_EMU_ENABLE_CLAP)
  add_library(
    mmt8_clap_wrapper STATIC
    frontends/clap/wrapper_stub.cpp
    third_party/emu8051/core.c
    third_party/emu8051/opcodes.c
    third_party/emu8051/disasm.c
  )
  target_link_libraries(mmt8_clap_wrapper PRIVATE mmt8_emu_core)
  set_target_properties(mmt8_clap_wrapper PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_include_directories(mmt8_clap_wrapper PUBLIC core/include third_party/emu8051)
  target_compile_options(mmt8_clap_wrapper PRIVATE -Wall -Wextra -Wpedantic)

  foreach(tool smoke matrix behavior mode_persistence mmio_probe state_diff)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/frontends/clap/wrapper_${tool}.cpp")
      add_executable(mmt8_clap_wrapper_${tool} frontends/clap/wrapper_${tool}.cpp)
      target_link_libraries(mmt8_clap_wrapper_${tool} PRIVATE mmt8_clap_wrapper)
      target_include_directories(mmt8_clap_wrapper_${tool} PRIVATE frontends/clap core/include)
      target_compile_options(mmt8_clap_wrapper_${tool} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endforeach()

  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/frontends/clap/emu8051_deep_audit.cpp")
    add_executable(
      mmt8_emu8051_deep_audit
      frontends/clap/emu8051_deep_audit.cpp
      third_party/emu8051/core.c
      third_party/emu8051/opcodes.c
      third_party/emu8051/disasm.c
    )
    target_include_directories(mmt8_emu8051_deep_audit PRIVATE frontends/clap core/include third_party/emu8051)
    target_compile_options(mmt8_emu8051_deep_audit PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  # The plugin ships with the OpenMMT-8 ROM built in (openmmt-8/tools/build_openmmt8_core.sh).
  set(MMT8_BUILTIN_ROM "${CMAKE_SOURCE_DIR}/../openmmt-8/build/openmmt8_rom.bin"
      CACHE FILEPATH "ROM image embedded in the CLAP plugin as its default")
  # The JAM-8 loop ROM (jam8/tools/build_jam8.sh) is built in as a second choice.
  set(MMT8_JAM_ROM "${CMAKE_SOURCE_DIR}/../jam8/build/jam8_rom.bin"
      CACHE FILEPATH "JAM-8 ROM image embedded in the CLAP plugin")
  set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")
  set(MMT8_GEN_DIR "${CMAKE_BINARY_DIR}/generated")
  function(mmt8_embed symbol input output)
    if(EXISTS "${input}")
      set(deps "${input}")
    else()
      message(WARNING "${input} not found; the plugin is built without it")
      set(deps "")
    endif()
    add_custom_command(
      OUTPUT "${output}"
      COMMAND "${CMAKE_COMMAND}" -DIN=${input} -DOUT=${output} -DSYMBOL=${symbol} -P
              "${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake"
      DEPENDS ${deps} "${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake"
      VERBATIM)
  endfunction()
  mmt8_embed(kBuiltinRom "${MMT8_BUILTIN_ROM}" "${MMT8_GEN_DIR}/builtin_rom.cpp")
  mmt8_embed(kJamRom "${MMT8_JAM_ROM}" "${MMT8_GEN_DIR}/jam_rom.cpp")
  mmt8_embed(kPanelFont "${IMGUI_DIR}/misc/fonts/Roboto-Medium.ttf" "${MMT8_GEN_DIR}/panel_font.cpp")

  add_library(
    mmt8_clap_plugin MODULE
    frontends/clap/plugin/clap_plugin.cpp
    frontends/clap/plugin/engine.cpp
    frontends/clap/plugin/project_state.cpp
    frontends/clap/plugin/rom_store.cpp
    frontends/clap/plugin/log.cpp
    "${MMT8_GEN_DIR}/builtin_rom.cpp"
    "${MMT8_GEN_DIR}/jam_rom.cpp"
    "${MMT8_GEN_DIR}/panel_font.cpp"
  )
  target_include_directories(mmt8_clap_plugin PRIVATE frontends/clap frontends/clap/plugin core/include third_party/clap/include)
  target_link_libraries(mmt8_clap_plugin PRIVATE mmt8_clap_wrapper)
  target_compile_options(mmt8_clap_plugin PRIVATE -Wall -Wextra)
  if(WIN32 OR MINGW)
    target_sources(
      mmt8_clap_plugin PRIVATE
      frontends/clap/plugin/gui_win32.cpp
      frontends/clap/plugin/panel_ui.cpp
      "${IMGUI_DIR}/imgui.cpp"
      "${IMGUI_DIR}/imgui_draw.cpp"
      "${IMGUI_DIR}/imgui_tables.cpp"
      "${IMGUI_DIR}/imgui_widgets.cpp"
      "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
      "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
    )
    target_include_directories(mmt8_clap_plugin PRIVATE "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
    target_compile_definitions(mmt8_clap_plugin PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(mmt8_clap_plugin PRIVATE gdi32 opengl32 imm32 dwmapi comdlg32)
  elseif(APPLE)
    enable_language(OBJCXX)
    set_target_properties(mmt8_clap_plugin PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED YES)
    target_sources(
      mmt8_clap_plugin PRIVATE
      frontends/clap/plugin/gui_cocoa.mm
      frontends/clap/plugin/panel_ui.cpp
      "${IMGUI_DIR}/imgui.cpp"
      "${IMGUI_DIR}/imgui_draw.cpp"
      "${IMGUI_DIR}/imgui_tables.cpp"
      "${IMGUI_DIR}/imgui_widgets.cpp"
      "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
    )
    set_source_files_properties(frontends/clap/plugin/gui_cocoa.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc -Wno-deprecated-declarations")
    target_include_directories(mmt8_clap_plugin PRIVATE "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
    target_link_libraries(mmt8_clap_plugin PRIVATE dl pthread "-framework Cocoa" "-framework OpenGL")
  else()
    find_package(X11 REQUIRED)
    find_package(OpenGL REQUIRED)
    target_sources(
      mmt8_clap_plugin PRIVATE
      frontends/clap/plugin/gui_x11.cpp
      frontends/clap/plugin/panel_ui.cpp
      "${IMGUI_DIR}/imgui.cpp"
      "${IMGUI_DIR}/imgui_draw.cpp"
      "${IMGUI_DIR}/imgui_tables.cpp"
      "${IMGUI_DIR}/imgui_widgets.cpp"
      "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
    )
    target_include_directories(mmt8_clap_plugin PRIVATE "${IMGUI_DIR}" "${IMGUI_DIR}/backends" ${X11_INCLUDE_DIR})
    target_link_libraries(mmt8_clap_plugin PRIVATE dl pthread ${X11_LIBRARIES} OpenGL::GL)
  endif()
  if(MINGW)
    target_link_options(mmt8_clap_plugin PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  set_target_properties(mmt8_clap_plugin PROPERTIES PREFIX "" SUFFIX ".clap"
                        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

  add_executable(mmt8_clap_host_test frontends/clap/plugin/host_test.cpp frontends/clap/plugin/project_state.cpp)
  target_include_directories(mmt8_clap_host_test PRIVATE frontends/clap/plugin third_party/clap/include)
  if(NOT WIN32)
    target_link_libraries(mmt8_clap_host_test PRIVATE dl)
  endif()
  add_dependencies(mmt8_clap_host_test mmt8_clap_plugin)

  add_executable(
    mmt8_clap_engine_test
    frontends/clap/plugin/engine_test.cpp
    frontends/clap/plugin/engine.cpp
    frontends/clap/plugin/project_state.cpp
    frontends/clap/plugin/rom_store.cpp
    frontends/clap/plugin/log.cpp
    "${MMT8_GEN_DIR}/builtin_rom.cpp"
    "${MMT8_GEN_DIR}/jam_rom.cpp"
  )
  target_include_directories(mmt8_clap_engine_test PRIVATE frontends/clap frontends/clap/plugin core/include third_party/clap/include)
  target_link_libraries(mmt8_clap_engine_test PRIVATE mmt8_clap_wrapper)
  if(NOT WIN32)
    target_link_libraries(mmt8_clap_engine_test PRIVATE dl pthread)
  endif()
  if(MINGW)
    target_link_options(mmt8_clap_host_test PRIVATE -static)
    target_link_options(mmt8_clap_engine_test PRIVATE -static)
    add_executable(mmt8_clap_gui_shot frontends/clap/plugin/gui_shot.cpp)
    target_include_directories(mmt8_clap_gui_shot PRIVATE third_party/clap/include)
    target_compile_definitions(mmt8_clap_gui_shot PRIVATE UNICODE _UNICODE)
    target_link_libraries(mmt8_clap_gui_shot PRIVATE gdi32 user32)
    target_link_options(mmt8_clap_gui_shot PRIVATE -static)
  endif()
endif()
