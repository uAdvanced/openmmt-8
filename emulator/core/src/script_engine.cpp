#include "mmt_emu/script_engine.hpp"

#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace mmt_emu {

static void schedule_model_events(Machine &machine, const std::string &name) {
    if (name == "saved_resume_restore_path") {
        machine.schedule_event(16, "saved_resume_restore");
        machine.schedule_event(15901, "event_edit_render_entry");
        return;
    }
    if (name == "event_list_render_probe") {
        machine.schedule_event(561, "event_list_render_entry");
        return;
    }
    if (name == "event_insert_program_record") {
        machine.schedule_event(53, "event_insert_program_record");
        return;
    }
    if (name == "saved_resume_plus_tape_save_path") {
        machine.schedule_event(1, "saved_resume_hold_reload");
        machine.schedule_event(2, "tape_save_entry");
        machine.schedule_event(3, "tape_timer1_isr");
        return;
    }
    if (name == "merge_order_stability_probe") {
        machine.schedule_event(35, "merge_order_stability");
        return;
    }
    if (name == "event_list_page_base_validation_probe") {
        machine.schedule_event(25, "event_list_page_base_validation");
        return;
    }
    if (name == "event_list_page_d5_transition_probe") {
        machine.schedule_event(25, "event_list_page_d5_transition");
        return;
    }
    if (name == "event_list_same_time_order_probe") {
        machine.schedule_event(25, "event_list_same_time_order");
        return;
    }
    if (name == "event_list_selection_after_copy_erase_probe") {
        machine.schedule_event(25, "event_list_selection_after_copy");
        machine.schedule_event(50, "event_list_selection_after_erase");
        return;
    }
    throw std::runtime_error("unknown model scenario: " + name);
}

std::vector<ScriptCommand> parse_script(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("failed to open scenario script: " + path);
    }
    std::vector<ScriptCommand> out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream iss(line);
        ScriptCommand cmd{};
        iss >> cmd.op;
        std::string arg;
        while (iss >> arg) {
            cmd.args.push_back(arg);
        }
        if (!cmd.op.empty()) {
            out.push_back(cmd);
        }
    }
    return out;
}

void apply_script(Machine &machine, const std::vector<ScriptCommand> &commands) {
    for (const auto &cmd : commands) {
        if (cmd.op == "watch") {
            if (cmd.args.size() < 2u) {
                throw std::runtime_error("watch requires: watch <pc> <event>");
            }
            const uint16_t pc = static_cast<uint16_t>(std::stoul(cmd.args[0], nullptr, 0));
            machine.add_watchpoint(pc, cmd.args[1]);
        } else if (cmd.op == "watch_once") {
            if (cmd.args.size() < 2u) {
                throw std::runtime_error("watch_once requires: watch_once <pc> <event>");
            }
            const uint16_t pc = static_cast<uint16_t>(std::stoul(cmd.args[0], nullptr, 0));
            machine.add_watchpoint_once(pc, cmd.args[1]);
        } else if (cmd.op == "emit") {
            if (cmd.args.size() < 2u) {
                throw std::runtime_error("emit requires: emit <cycle> <event>");
            }
            const uint64_t cycle = std::stoull(cmd.args[0], nullptr, 0);
            machine.schedule_event(cycle, cmd.args[1]);
        } else if (cmd.op == "emit_stride") {
            if (cmd.args.size() < 4u) {
                throw std::runtime_error("emit_stride requires: emit_stride <start> <stride> <count> <event>");
            }
            const uint64_t start_cycle = std::stoull(cmd.args[0], nullptr, 0);
            const uint64_t stride = std::stoull(cmd.args[1], nullptr, 0);
            const uint64_t count = std::stoull(cmd.args[2], nullptr, 0);
            const std::string &event = cmd.args[3];
            for (uint64_t i = 0; i < count; ++i) {
                machine.schedule_event(start_cycle + (i * stride), event);
            }
        } else if (cmd.op == "include_trace") {
            if (cmd.args.empty()) {
                throw std::runtime_error("include_trace requires: include_trace <jsonl_path>");
            }
            std::ifstream in(cmd.args[0]);
            if (!in) {
                throw std::runtime_error("failed to open include_trace file: " + cmd.args[0]);
            }
            const std::regex pattern("\\{\"cycle\":([0-9]+),\"event\":\"([^\"]+)\"");
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty()) {
                    continue;
                }
                std::smatch m;
                if (!std::regex_search(line, m, pattern)) {
                    continue;
                }
                const uint64_t cycle = std::stoull(m[1].str(), nullptr, 10);
                const std::string event = m[2].str();
                machine.schedule_event(cycle, event);
            }
        } else if (cmd.op == "model") {
            if (cmd.args.empty()) {
                throw std::runtime_error("model requires: model <scenario_name>");
            }
            schedule_model_events(machine, cmd.args[0]);
        } else {
            throw std::runtime_error("unknown script command: " + cmd.op);
        }
    }
}

}  // namespace mmt_emu
