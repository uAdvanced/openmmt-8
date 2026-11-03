#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mmt8 {

/** Everything a DAW project keeps for one MMT-8: the ROM, the battery-backed RAM and settings. */
struct ProjectState {
    bool builtin_rom = true;
    bool builtin_jam8 = false;     // with builtin_rom: the built-in ROM is JAM-8
    std::string rom_name;          // file name shown in the GUI
    std::vector<uint8_t> rom;      // ROM image when builtin_rom is false
    std::vector<uint8_t> ram;      // battery-backed RAM; empty means factory-fresh
    /** The memory of the other ROMs used in this project, by RomStore key. */
    std::map<std::string, std::vector<uint8_t>> other_ram;
    bool host_sync = true;
    float click_level = 0.8f;
    uint32_t gui_width = 0;        // 0 means default size
    /** Set when the chunk came from the old plugin, which saved only a ROM path. */
    std::string legacy_rom_path;
};

std::vector<uint8_t> encode_state(const ProjectState &s);
bool decode_state(const std::vector<uint8_t> &chunk, ProjectState &out);

std::vector<uint8_t> rle_encode(const std::vector<uint8_t> &in);
bool rle_decode(const uint8_t *in, size_t size, std::vector<uint8_t> &out, size_t max_out);

}  // namespace mmt8
