#pragma once

#include <string>
#include <vector>

#include "mmt_emu/machine.hpp"

namespace mmt_emu {

struct ScriptCommand {
    std::string op;
    std::vector<std::string> args;
};

std::vector<ScriptCommand> parse_script(const std::string &path);
void apply_script(Machine &machine, const std::vector<ScriptCommand> &commands);

}  // namespace mmt_emu
