#include "mmt_emu/rom_image.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace mmt_emu {

namespace {

uint8_t parse_hex_byte(const std::string &text) {
    if (text.size() != 2u) {
        throw std::runtime_error("invalid hex byte: " + text);
    }
    unsigned int out = 0;
    std::stringstream ss;
    ss << std::hex << text;
    ss >> out;
    return static_cast<uint8_t>(out & 0xffu);
}

bool has_suffix(const std::string &value, const std::string &suffix) {
    if (value.size() < suffix.size()) {
        return false;
    }
    return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

std::vector<uint8_t> parse_intel_hex(std::ifstream &in) {
    std::vector<uint8_t> image(65536u, 0xffu);
    std::string line;
    uint32_t upper = 0u;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        if (line[0] != ':') {
            throw std::runtime_error("invalid ihex line (missing ':')");
        }
        if (line.size() < 11u) {
            throw std::runtime_error("invalid ihex line length");
        }
        const uint8_t len = parse_hex_byte(line.substr(1, 2));
        const uint16_t addr = static_cast<uint16_t>((parse_hex_byte(line.substr(3, 2)) << 8u) | parse_hex_byte(line.substr(5, 2)));
        const uint8_t type = parse_hex_byte(line.substr(7, 2));
        const size_t expected = 11u + static_cast<size_t>(len) * 2u;
        if (line.size() < expected) {
            throw std::runtime_error("invalid ihex data length");
        }
        if (type == 0x00u) {
            for (uint8_t i = 0; i < len; ++i) {
                const uint8_t value = parse_hex_byte(line.substr(9u + static_cast<size_t>(i) * 2u, 2));
                const uint32_t full = upper + static_cast<uint32_t>(addr) + static_cast<uint32_t>(i);
                if (full < image.size()) {
                    image[full] = value;
                }
            }
        } else if (type == 0x01u) {
            break;
        } else if (type == 0x04u && len == 2u) {
            upper = static_cast<uint32_t>(
                        (parse_hex_byte(line.substr(9, 2)) << 8u) | parse_hex_byte(line.substr(11, 2)))
                    << 16u;
        }
    }
    return image;
}

}  // namespace

RomImage RomImage::load_file(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open ROM: " + path);
    }
    std::vector<uint8_t> data;
    if (has_suffix(path, ".ihx") || has_suffix(path, ".hex")) {
        in.close();
        std::ifstream text_in(path);
        if (!text_in) {
            throw std::runtime_error("failed to open ihex ROM: " + path);
        }
        data = parse_intel_hex(text_in);
    } else {
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (data.empty()) {
        throw std::runtime_error("ROM is empty: " + path);
    }
    return RomImage(std::move(data));
}

RomImage::RomImage(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}

const std::vector<uint8_t> &RomImage::bytes() const { return bytes_; }

size_t RomImage::size() const { return bytes_.size(); }

}  // namespace mmt_emu
