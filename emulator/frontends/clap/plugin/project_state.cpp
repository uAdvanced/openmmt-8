#include "project_state.h"

#include <cstring>

namespace mmt8 {
namespace {

constexpr char kMagic[4] = {'M', 'M', 'T', '8'};
constexpr uint32_t kVersion = 1;
constexpr size_t kMaxBlob = 1u << 20;

void put32(std::vector<uint8_t> &o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void put_blob(std::vector<uint8_t> &o, const void *p, size_t n) {
    put32(o, static_cast<uint32_t>(n));
    const auto *b = static_cast<const uint8_t *>(p);
    o.insert(o.end(), b, b + n);
}

struct Reader {
    const std::vector<uint8_t> &d;
    size_t at = 0;
    bool ok = true;

    uint32_t u32() {
        if (at + 4 > d.size()) {
            ok = false;
            return 0;
        }
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(d[at++]) << (8 * i);
        return v;
    }
    const uint8_t *blob(size_t &n) {
        n = u32();
        if (!ok || n > kMaxBlob || at + n > d.size()) {
            ok = false;
            return nullptr;
        }
        const uint8_t *p = d.data() + at;
        at += n;
        return p;
    }
};

/* The old plugin wrote: u32 path length, path, u32 runtime blob length, blob. */
bool decode_legacy(const std::vector<uint8_t> &chunk, ProjectState &out) {
    Reader r{chunk};
    size_t n = 0;
    const uint8_t *path = r.blob(n);
    if (!r.ok || n > 4096) return false;
    out = ProjectState{};
    out.legacy_rom_path.assign(reinterpret_cast<const char *>(path), n);
    return true;
}

}  // namespace

std::vector<uint8_t> rle_encode(const std::vector<uint8_t> &in) {
    /* PackBits: c < 0x80 copies c+1 literal bytes; c >= 0x80 repeats the next byte c-0x7D times. */
    std::vector<uint8_t> out;
    out.reserve(in.size() / 4 + 16);
    size_t i = 0;
    while (i < in.size()) {
        size_t run = 1;
        while (i + run < in.size() && run < 130 && in[i + run] == in[i]) ++run;
        if (run >= 3) {
            out.push_back(static_cast<uint8_t>(0x7D + run));
            out.push_back(in[i]);
            i += run;
            continue;
        }
        const size_t start = i;
        while (i < in.size() && i - start < 128) {
            if (i + 2 < in.size() && in[i] == in[i + 1] && in[i] == in[i + 2]) break;
            ++i;
        }
        out.push_back(static_cast<uint8_t>(i - start - 1));
        out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(start), in.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return out;
}

bool rle_decode(const uint8_t *in, size_t size, std::vector<uint8_t> &out, size_t max_out) {
    out.clear();
    size_t i = 0;
    while (i < size) {
        const uint8_t c = in[i++];
        if (c < 0x80) {
            const size_t n = static_cast<size_t>(c) + 1;
            if (i + n > size || out.size() + n > max_out) return false;
            out.insert(out.end(), in + i, in + i + n);
            i += n;
        } else {
            const size_t n = static_cast<size_t>(c) - 0x7D;
            if (i >= size || out.size() + n > max_out) return false;
            out.insert(out.end(), n, in[i++]);
        }
    }
    return true;
}

std::vector<uint8_t> encode_state(const ProjectState &s) {
    std::vector<uint8_t> o;
    o.insert(o.end(), kMagic, kMagic + 4);
    put32(o, kVersion);
    put32(o, (s.builtin_rom ? 1u : 0u) | (s.host_sync ? 2u : 0u) | (s.builtin_rom && s.builtin_jam8 ? 4u : 0u));
    uint32_t click = 0;
    std::memcpy(&click, &s.click_level, 4);
    put32(o, click);
    put32(o, s.gui_width);
    put_blob(o, s.rom_name.data(), s.rom_name.size());
    if (s.builtin_rom) put32(o, 0);
    else put_blob(o, s.rom.data(), s.rom.size());
    const std::vector<uint8_t> ram = rle_encode(s.ram);
    put32(o, static_cast<uint32_t>(s.ram.size()));
    put_blob(o, ram.data(), ram.size());
    /* Appended after the version 1 fields, which older plugins read and stop after. */
    if (!s.other_ram.empty()) {
        put32(o, static_cast<uint32_t>(s.other_ram.size()));
        for (const auto &[key, mem] : s.other_ram) {
            put_blob(o, key.data(), key.size());
            const std::vector<uint8_t> packed = rle_encode(mem);
            put32(o, static_cast<uint32_t>(mem.size()));
            put_blob(o, packed.data(), packed.size());
        }
    }
    return o;
}

bool decode_state(const std::vector<uint8_t> &chunk, ProjectState &out) {
    if (chunk.size() < 8 || std::memcmp(chunk.data(), kMagic, 4) != 0) return decode_legacy(chunk, out);
    Reader r{chunk, 4};
    const uint32_t version = r.u32();
    if (!r.ok || version == 0 || version > kVersion) return false;
    ProjectState s;
    const uint32_t flags = r.u32();
    s.builtin_rom = (flags & 1u) != 0;
    s.host_sync = (flags & 2u) != 0;
    s.builtin_jam8 = s.builtin_rom && (flags & 4u) != 0;
    const uint32_t click = r.u32();
    std::memcpy(&s.click_level, &click, 4);
    if (!(s.click_level >= 0.0f && s.click_level <= 1.0f)) s.click_level = 0.8f;
    s.gui_width = r.u32();
    size_t n = 0;
    const uint8_t *p = r.blob(n);
    if (!r.ok) return false;
    s.rom_name.assign(reinterpret_cast<const char *>(p), n);
    p = r.blob(n);
    if (!r.ok) return false;
    s.rom.assign(p, p + n);
    const uint32_t ram_size = r.u32();
    p = r.blob(n);
    if (!r.ok || ram_size > 0x10000u) return false;
    if (!rle_decode(p, n, s.ram, ram_size) || s.ram.size() != ram_size) return false;
    if (!s.builtin_rom && s.rom.empty()) return false;
    if (r.at < chunk.size()) {
        const uint32_t count = r.u32();
        for (uint32_t i = 0; r.ok && i < count && i < 64; ++i) {
            p = r.blob(n);
            if (!r.ok || n > 32) return false;
            std::string key(reinterpret_cast<const char *>(p), n);
            const uint32_t size = r.u32();
            p = r.blob(n);
            std::vector<uint8_t> mem;
            if (!r.ok || size > 0x10000u || !rle_decode(p, n, mem, size) || mem.size() != size) return false;
            s.other_ram[key] = std::move(mem);
        }
        if (!r.ok) return false;
    }
    out = std::move(s);
    return true;
}

}  // namespace mmt8
