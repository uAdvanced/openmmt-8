#pragma once

#include <string>
#include <vector>

#include "mmt_emu/interfaces.hpp"

namespace mmt_emu {

class RomImage final : public IRomImage {
public:
    static RomImage load_file(const std::string &path);

    explicit RomImage(std::vector<uint8_t> bytes);

    const std::vector<uint8_t> &bytes() const override;
    size_t size() const override;

private:
    std::vector<uint8_t> bytes_;
};

}  // namespace mmt_emu
