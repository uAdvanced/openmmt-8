#include "rom_store.h"

#include <cstdio>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#include "log.h"

namespace mmt8 {
namespace {

#if defined(_WIN32)
std::wstring wide(const std::string &s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    w.resize(w.size() - 1);
    return w;
}
#endif

FILE *open_file(const std::string &path, const char *mode) {
#if defined(_WIN32)
    return _wfopen(wide(path).c_str(), wide(mode).c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

void make_dir(const std::string &dir) {
#if defined(_WIN32)
    CreateDirectoryW(wide(dir).c_str(), nullptr);
#else
    mkdir(dir.c_str(), 0755);
#endif
}

bool replace_file(const std::string &from, const std::string &to) {
#if defined(_WIN32)
    return MoveFileExW(wide(from).c_str(), wide(to).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return std::rename(from.c_str(), to.c_str()) == 0;
#endif
}

bool safe_key(const std::string &key) {
    if (key.empty() || key.size() > 32) return false;
    for (char c : key)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'))) return false;
    return true;
}

}  // namespace

bool read_file(const std::string &path, std::vector<uint8_t> &out, size_t max_bytes) {
    FILE *f = open_file(path, "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[16384];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0 && out.size() <= max_bytes) out.insert(out.end(), buf, buf + got);
    std::fclose(f);
    return true;
}

/* Written beside the target and renamed over it, so a crash leaves the old file whole. */
bool write_file(const std::string &path, const std::vector<uint8_t> &data) {
    const std::string tmp = path + ".tmp";
    FILE *f = open_file(tmp, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    if (std::fclose(f) != 0 || !ok) return false;
    return replace_file(tmp, path);
}

RomStore::RomStore(std::string dir) : dir_(std::move(dir)) {
    if (!dir_.empty()) make_dir(dir_);
}

std::string RomStore::key_of(const std::vector<uint8_t> &image) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (uint8_t b : image) h = (h ^ b) * 0x100000001B3ull;
    char s[17];
    std::snprintf(s, sizeof(s), "%016llx", static_cast<unsigned long long>(h));
    return s;
}

std::string RomStore::path(const std::string &file) const {
    const char sep = dir_.find('\\') != std::string::npos ? '\\' : '/';
    return dir_.back() == '/' || dir_.back() == '\\' ? dir_ + file : dir_ + sep + file;
}

bool RomStore::last(std::string &key, std::string &name) const {
    std::vector<uint8_t> d;
    if (!enabled() || !read_file(path("last_rom.txt"), d, 4096)) return false;
    const std::string s(d.begin(), d.end());
    const size_t nl = s.find('\n');
    if (nl == std::string::npos) return false;
    key = s.substr(0, nl);
    name = s.substr(nl + 1);
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
    return safe_key(key);
}

void RomStore::set_last(const std::string &key, const std::string &name) {
    if (!enabled() || !safe_key(key)) return;
    const std::string s = key + "\n" + name + "\n";
    if (!write_file(path("last_rom.txt"), std::vector<uint8_t>(s.begin(), s.end())))
        log("store: cannot write %s", path("last_rom.txt").c_str());
}

bool RomStore::load_rom(const std::string &key, std::vector<uint8_t> &image) const {
    return enabled() && safe_key(key) && read_file(path("rom_" + key + ".bin"), image) && !image.empty() &&
           image.size() <= 0x10000u;
}

void RomStore::save_rom(const std::string &key, const std::vector<uint8_t> &image) {
    if (!enabled() || !safe_key(key) || key == kBuiltinKey || key == kJamKey) return;
    std::vector<uint8_t> have;
    if (load_rom(key, have) && have == image) return;
    if (!write_file(path("rom_" + key + ".bin"), image)) log("store: cannot write the copy of ROM %s", key.c_str());
}

bool RomStore::load_ram(const std::string &key, std::vector<uint8_t> &ram) const {
    return enabled() && safe_key(key) && read_file(path("ram_" + key + ".bin"), ram, 0x10000u);
}

void RomStore::save_ram(const std::string &key, const std::vector<uint8_t> &ram) {
    if (!enabled() || !safe_key(key) || ram.empty()) return;
    if (!write_file(path("ram_" + key + ".bin"), ram)) log("store: cannot write the memory of ROM %s", key.c_str());
}

}  // namespace mmt8
