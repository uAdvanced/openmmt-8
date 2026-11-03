#pragma once

#include "mmt_emu/plugin_port.hpp"

extern "C" mmt_emu::IPluginRuntime *mmt8_clap_wrapper_create();
extern "C" void mmt8_clap_wrapper_destroy(mmt_emu::IPluginRuntime *runtime);
extern "C" bool mmt8_clap_wrapper_debug_set_ffxx(mmt_emu::IPluginRuntime *runtime, uint16_t address, uint8_t value);
extern "C" bool mmt8_clap_wrapper_debug_serial_rx(mmt_emu::IPluginRuntime *runtime, uint8_t value);
extern "C" bool mmt8_clap_wrapper_debug_uart_rx(mmt_emu::IPluginRuntime *runtime, uint8_t value);
extern "C" bool mmt8_clap_wrapper_debug_set_pc(mmt_emu::IPluginRuntime *runtime, uint16_t pc);
extern "C" bool mmt8_clap_wrapper_debug_set_iram(mmt_emu::IPluginRuntime *runtime, uint8_t address, uint8_t value);
extern "C" bool mmt8_clap_wrapper_debug_set_sfr(mmt_emu::IPluginRuntime *runtime, uint8_t sfr_addr, uint8_t value);
extern "C" bool mmt8_clap_wrapper_debug_set_xram(mmt_emu::IPluginRuntime *runtime, uint16_t address, uint8_t value);
extern "C" bool mmt8_clap_wrapper_debug_peek_iram(mmt_emu::IPluginRuntime *runtime, uint8_t address, uint8_t *value_out);
extern "C" bool mmt8_clap_wrapper_debug_peek_sfr(mmt_emu::IPluginRuntime *runtime, uint8_t sfr_addr, uint8_t *value_out);
extern "C" bool mmt8_clap_wrapper_debug_peek_xram(mmt_emu::IPluginRuntime *runtime, uint16_t address, uint8_t *value_out);
/* Record the machine cycle and stack pointer each time an instruction at a watched PC starts. */
extern "C" void mmt8_clap_wrapper_debug_watch_pc(mmt_emu::IPluginRuntime *runtime, uint16_t pc);
extern "C" uint32_t mmt8_clap_wrapper_debug_drain_pc_hits(
    mmt_emu::IPluginRuntime *runtime, uint64_t *cycles, uint16_t *pcs, uint8_t *sps, uint32_t max);
/* Highest stack pointer value reached since the ROM was loaded. */
extern "C" uint8_t mmt8_clap_wrapper_debug_sp_max(mmt_emu::IPluginRuntime *runtime);
extern "C" void mmt8_clap_wrapper_debug_enable_panel_write_log(mmt_emu::IPluginRuntime *runtime, bool on);
extern "C" uint32_t mmt8_clap_wrapper_debug_drain_panel_writes(
    mmt_emu::IPluginRuntime *runtime,
    uint32_t *out,
    uint32_t max_entries);
extern "C" uint32_t mmt8_clap_wrapper_debug_drain_serial_tx(
    mmt_emu::IPluginRuntime *runtime,
    uint8_t *bytes_out,
    uint32_t max_bytes);
