#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mmt8 {

/** Whole-file read and replace; paths are UTF-8 on every platform. */
bool read_file(const std::string &path, std::vector<uint8_t> &out, size_t max_bytes = 0x10000u);
bool write_file(const std::string &path, const std::vector<uint8_t> &data);

/**
 * What the plugin keeps between sessions, in one folder: the ROM last chosen, a copy of each
 * loaded ROM file (so the choice survives the file moving), and the battery-backed RAM last
 * used with each ROM. A ROM is known by its key: "builtin" (OpenMMT-8), "builtinjam8", or a hash of
 * its image.
 * An empty folder name turns the store off.
 */
class RomStore {
public:
    static constexpr const char *kBuiltinKey = "builtin";
    static constexpr const char *kJamKey = "builtinjam8";

    explicit RomStore(std::string dir);
    bool enabled() const { return !dir_.empty(); }
    static std::string key_of(const std::vector<uint8_t> &image);

    bool last(std::string &key, std::string &name) const;
    void set_last(const std::string &key, const std::string &name);
    bool load_rom(const std::string &key, std::vector<uint8_t> &image) const;
    void save_rom(const std::string &key, const std::vector<uint8_t> &image);
    bool load_ram(const std::string &key, std::vector<uint8_t> &ram) const;
    void save_ram(const std::string &key, const std::vector<uint8_t> &ram);

private:
    std::string path(const std::string &file) const;
    std::string dir_;
};

}  // namespace mmt8
