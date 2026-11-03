#include "mmt_emu/machine.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace mmt_emu {

namespace {

constexpr uint8_t opcode_cycles(uint8_t opcode) {
    switch (opcode) {
        case 0x00: return 1;  // NOP
        case 0x02: return 2;  // LJMP
        case 0x12: return 2;  // LCALL
        case 0x22: return 2;  // RET
        case 0x32: return 2;  // RETI
        case 0x73: return 2;  // JMP @A+DPTR
        case 0xB4: return 2;  // CJNE A,#data,rel
        case 0x40: return 2;  // JC rel
        case 0x50: return 2;  // JNC rel
        case 0x60: return 2;  // JZ rel
        case 0x70: return 2;  // JNZ rel
        case 0x74: return 1;  // MOV A,#data
        case 0xE4: return 1;  // CLR A
        case 0x04: return 1;  // INC A
        case 0x14: return 1;  // DEC A
        case 0x44: return 1;  // ORL A,#data
        case 0x54: return 1;  // ANL A,#data
        case 0x64: return 1;  // XRL A,#data
        case 0x24: return 1;  // ADD A,#data
        case 0x34: return 1;  // ADDC A,#data
        case 0x94: return 1;  // SUBB A,#data
        case 0xC3: return 1;  // CLR C
        case 0xD3: return 1;  // SETB C
        case 0xB3: return 1;  // CPL C
        case 0x84: return 4;  // DIV AB
        case 0xA4: return 4;  // MUL AB
        case 0x90: return 2;  // MOV DPTR,#data16
        case 0xA3: return 2;  // INC DPTR
        case 0xE0: return 2;  // MOVX A,@DPTR
        case 0xF0: return 2;  // MOVX @DPTR,A
        case 0xE2: return 2;  // MOVX A,@R0
        case 0xE3: return 2;  // MOVX A,@R1
        case 0xF2: return 2;  // MOVX @R0,A
        case 0xF3: return 2;  // MOVX @R1,A
        default: return 1;    // conservative placeholder until full conformance model
    }
}

constexpr uint8_t opcode_length(uint8_t opcode) {
    switch (opcode) {
        case 0x02:  // LJMP
        case 0x12:  // LCALL
        case 0x90:  // MOV DPTR,#data16
            return 3;
        case 0x01:  // AJMP page0
        case 0x11:  // ACALL page0
        case 0x21:  // AJMP page1
        case 0x31:  // ACALL page1
        case 0x41:  // AJMP page2
        case 0x51:  // ACALL page2
        case 0x61:  // AJMP page3
        case 0x71:  // ACALL page3
        case 0x81:  // AJMP page4
        case 0x91:  // ACALL page4
        case 0xA1:  // AJMP page5
        case 0xB1:  // ACALL page5
        case 0xC1:  // AJMP page6
        case 0xD1:  // ACALL page6
        case 0xE1:  // AJMP page7
        case 0xF1:  // ACALL page7
        case 0x80:  // SJMP rel
        case 0x44:  // ORL A,#data
        case 0x54:  // ANL A,#data
        case 0x64:  // XRL A,#data
        case 0x74:  // MOV A,#data
        case 0x24:  // ADD A,#data
        case 0x34:  // ADDC A,#data
        case 0x94:  // SUBB A,#data
        case 0xA2:  // MOV C,bit
        case 0x92:  // MOV bit,C
        case 0xB2:  // CPL bit
        case 0xC2:  // CLR bit
        case 0xD2:  // SETB bit
        case 0x10:  // JBC bit,rel
        case 0x20:  // JB bit,rel
        case 0x30:  // JNB bit,rel
        case 0x40:  // JC rel
        case 0x50:  // JNC rel
        case 0x60:  // JZ rel
        case 0x70:  // JNZ rel
        case 0xD5:  // DJNZ direct,rel
            return 2;
        case 0xB4:  // CJNE A,#data,rel
            return 3;
        default:
            return 1;
    }
}

uint16_t interrupt_vector_for_event(const std::string &event_tag) {
    if (event_tag == "isr_serial_vector") {
        return 0x0023u;
    }
    if (event_tag.find("timer1_isr") != std::string::npos) {
        return 0x001Bu;
    }
    if (event_tag.find("timer_tick") != std::string::npos) {
        return 0x000Bu;
    }
    if (event_tag.rfind("isr_", 0u) == 0u) {
        return 0x0003u;
    }
    return 0u;
}

}  // namespace

FlatBus::FlatBus(RomImage rom) : rom_(std::move(rom)) {}

uint8_t FlatBus::read_code(uint16_t address) const {
    const size_t idx = static_cast<size_t>(address);
    if (idx >= rom_.size()) {
        return 0xff;
    }
    return rom_.bytes()[idx];
}

uint8_t FlatBus::read_data(uint8_t address) const { return iram_[address]; }
uint8_t FlatBus::read_xdata(uint16_t address) const { return xram_[address]; }

void FlatBus::write_data(uint8_t address, uint8_t value) { iram_[address] = value; }

void FlatBus::write_sfr(uint8_t address, uint8_t value) { sfr_[address & 0x7fu] = value; }

void FlatBus::write_xdata(uint16_t address, uint8_t value) {
    xram_[address] = value;
    if (address == 0xFF08u) {
        lcd_cmd(value);
    } else if (address == 0xFF09u) {
        lcd_data(value);
    }
}

void FlatBus::reset_runtime_state() {
    for (uint8_t &v : iram_) {
        v = 0u;
    }
    for (uint8_t &v : sfr_) {
        v = 0u;
    }
    for (uint8_t &v : xram_) {
        v = 0u;
    }
    for (uint8_t &c : lcd_ddram_) {
        c = static_cast<uint8_t>(' ');
    }
    lcd_addr_ = 0u;
    lcd_entry_inc_ = true;
}

void FlatBus::lcd_cmd(uint8_t value) {
    if (value == 0x01u) {  // clear display
        for (uint8_t &c : lcd_ddram_) {
            c = static_cast<uint8_t>(' ');
        }
        lcd_addr_ = 0u;
        return;
    }
    if (value == 0x02u) {  // return home
        lcd_addr_ = 0u;
        return;
    }
    if ((value & 0x80u) != 0u) {  // set DDRAM address
        lcd_addr_ = static_cast<uint8_t>(value & 0x7Fu);
        return;
    }
    if ((value & 0x04u) == 0x04u) {  // entry mode set
        lcd_entry_inc_ = (value & 0x02u) != 0u;
    }
}

void FlatBus::lcd_data(uint8_t value) {
    const uint8_t idx = static_cast<uint8_t>(lcd_addr_ % 80u);
    lcd_ddram_[idx] = value;
    if (lcd_entry_inc_) {
        lcd_addr_ = static_cast<uint8_t>((lcd_addr_ + 1u) % 80u);
    } else {
        lcd_addr_ = static_cast<uint8_t>((lcd_addr_ - 1u) % 80u);
    }
}

std::string FlatBus::lcd_line1() const {
    std::string out(16u, ' ');
    for (size_t i = 0; i < 16u; ++i) {
        const uint8_t c = lcd_ddram_[i];
        out[i] = (c >= 32u && c <= 126u) ? static_cast<char>(c) : ' ';
    }
    return out;
}

std::string FlatBus::lcd_line2() const {
    std::string out(16u, ' ');
    for (size_t i = 0; i < 16u; ++i) {
        const uint8_t c = lcd_ddram_[0x40u + i];
        out[i] = (c >= 32u && c <= 126u) ? static_cast<char>(c) : ' ';
    }
    return out;
}

StepCpuCore::StepCpuCore(IBus &bus) : bus_(bus) {}

void StepCpuCore::reset(uint16_t pc) {
    pc_ = pc;
    state_ = CpuCoreState{};
    state_.sp = 0x07u;
    state_.dptr = 0u;
}

uint32_t StepCpuCore::step() {
    const uint8_t opcode = bus_.read_code(pc_);
    opcode_histogram_[opcode] += 1u;
    const uint16_t base_pc = pc_;
    const uint8_t len = opcode_length(opcode);
    uint16_t next_pc = static_cast<uint16_t>(pc_ + len);
    switch (opcode) {
        case 0x02: {  // LJMP addr16
            const uint8_t hi = bus_.read_code(static_cast<uint16_t>(base_pc + 1u));
            const uint8_t lo = bus_.read_code(static_cast<uint16_t>(base_pc + 2u));
            next_pc = static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8u) | static_cast<uint16_t>(lo));
            break;
        }
        case 0x12: {  // LCALL addr16
            const uint8_t hi = bus_.read_code(static_cast<uint16_t>(base_pc + 1u));
            const uint8_t lo = bus_.read_code(static_cast<uint16_t>(base_pc + 2u));
            const uint16_t ret = next_pc;
            state_.sp = static_cast<uint8_t>(state_.sp + 1u);
            bus_.write_data(state_.sp, static_cast<uint8_t>((ret >> 8u) & 0xFFu));
            state_.sp = static_cast<uint8_t>(state_.sp + 1u);
            bus_.write_data(state_.sp, static_cast<uint8_t>(ret & 0xFFu));
            next_pc = static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8u) | static_cast<uint16_t>(lo));
            state_.call_depth = static_cast<uint16_t>(state_.call_depth + 1u);
            break;
        }
        case 0x22: {  // RET
            const uint8_t lo = bus_.read_data(state_.sp);
            state_.sp = static_cast<uint8_t>(state_.sp - 1u);
            const uint8_t hi = bus_.read_data(state_.sp);
            state_.sp = static_cast<uint8_t>(state_.sp - 1u);
            next_pc = static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8u) | static_cast<uint16_t>(lo));
            if (state_.call_depth > 0u) {
                state_.call_depth = static_cast<uint16_t>(state_.call_depth - 1u);
            }
            break;
        }
        case 0x80: {  // SJMP rel
            const int8_t rel = static_cast<int8_t>(bus_.read_code(static_cast<uint16_t>(base_pc + 1u)));
            next_pc = static_cast<uint16_t>(next_pc + rel);
            break;
        }
        case 0x74: {  // MOV A,#data
            state_.acc = bus_.read_code(static_cast<uint16_t>(base_pc + 1u));
            break;
        }
        case 0x90: {  // MOV DPTR,#data16
            const uint8_t hi = bus_.read_code(static_cast<uint16_t>(base_pc + 1u));
            const uint8_t lo = bus_.read_code(static_cast<uint16_t>(base_pc + 2u));
            state_.dptr = static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8u) | static_cast<uint16_t>(lo));
            break;
        }
        case 0xA3:  // INC DPTR
            state_.dptr = static_cast<uint16_t>(state_.dptr + 1u);
            break;
        case 0xE0:  // MOVX A,@DPTR
            state_.acc = bus_.read_xdata(state_.dptr);
            break;
        case 0xF0:  // MOVX @DPTR,A
            bus_.write_xdata(state_.dptr, state_.acc);
            break;
        case 0xE2:  // MOVX A,@R0
            state_.acc = bus_.read_xdata(bus_.read_data(0u));
            break;
        case 0xE3:  // MOVX A,@R1
            state_.acc = bus_.read_xdata(bus_.read_data(1u));
            break;
        case 0xF2:  // MOVX @R0,A
            bus_.write_xdata(bus_.read_data(0u), state_.acc);
            break;
        case 0xF3:  // MOVX @R1,A
            bus_.write_xdata(bus_.read_data(1u), state_.acc);
            break;
        case 0xE4:  // CLR A
            state_.acc = 0u;
            break;
        case 0x04:  // INC A
            state_.acc = static_cast<uint8_t>(state_.acc + 1u);
            break;
        case 0x14:  // DEC A
            state_.acc = static_cast<uint8_t>(state_.acc - 1u);
            break;
        case 0x44: {  // ORL A,#data
            const uint8_t imm = bus_.read_code(static_cast<uint16_t>(base_pc + 1u));
            state_.acc = static_cast<uint8_t>(state_.acc | imm);
            break;
        }
        case 0x54: {  // ANL A,#data
            const uint8_t imm = bus_.read_code(static_cast<uint16_t>(base_pc + 1u));
            state_.acc = static_cast<uint8_t>(state_.acc & imm);
            break;
        }
        case 0x64: {  // XRL A,#data
            const uint8_t imm = bus_.read_code(static_cast<uint16_t>(base_pc + 1u));
            state_.acc = static_cast<uint8_t>(state_.acc ^ imm);
            break;
        }
        case 0x24: {  // ADD A,#data
            const uint8_t imm = bus_.read_code(static_cast<uint16_t>(base_pc + 1u));
            const uint16_t sum = static_cast<uint16_t>(state_.acc) + static_cast<uint16_t>(imm);
            state_.acc = static_cast<uint8_t>(sum & 0xFFu);
            state_.carry = sum > 0xFFu;
            break;
        }
        case 0x34: {  // ADDC A,#data
            const uint8_t imm = bus_.read_code(static_cast<uint16_t>(base_pc + 1u));
            const uint16_t c = state_.carry ? 1u : 0u;
            const uint16_t sum = static_cast<uint16_t>(state_.acc) + static_cast<uint16_t>(imm) + c;
            state_.acc = static_cast<uint8_t>(sum & 0xFFu);
            state_.carry = sum > 0xFFu;
            break;
        }
        case 0x94: {  // SUBB A,#data
            const uint8_t imm = bus_.read_code(static_cast<uint16_t>(base_pc + 1u));
            const uint16_t c = state_.carry ? 1u : 0u;
            const uint16_t lhs = static_cast<uint16_t>(state_.acc);
            const uint16_t rhs = static_cast<uint16_t>(imm) + c;
            const int16_t diff = static_cast<int16_t>(lhs) - static_cast<int16_t>(rhs);
            state_.acc = static_cast<uint8_t>(diff & 0xFF);
            state_.carry = lhs < rhs;
            break;
        }
        case 0xC3:  // CLR C
            state_.carry = false;
            break;
        case 0xD3:  // SETB C
            state_.carry = true;
            break;
        case 0xB3:  // CPL C
            state_.carry = !state_.carry;
            break;
        case 0xB4: {  // CJNE A,#data,rel
            const uint8_t imm = bus_.read_code(static_cast<uint16_t>(base_pc + 1u));
            const int8_t rel = static_cast<int8_t>(bus_.read_code(static_cast<uint16_t>(base_pc + 2u)));
            state_.carry = state_.acc < imm;
            if (state_.acc != imm) {
                next_pc = static_cast<uint16_t>(next_pc + rel);
            }
            break;
        }
        case 0xD5: {  // DJNZ direct,rel
            const uint8_t direct = bus_.read_code(static_cast<uint16_t>(base_pc + 1u));
            const int8_t rel = static_cast<int8_t>(bus_.read_code(static_cast<uint16_t>(base_pc + 2u)));
            uint8_t value = bus_.read_data(direct);
            value = static_cast<uint8_t>(value - 1u);
            bus_.write_data(direct, value);
            if (value != 0u) {
                next_pc = static_cast<uint16_t>(next_pc + rel);
            }
            break;
        }
        case 0x40: {  // JC rel
            const int8_t rel = static_cast<int8_t>(bus_.read_code(static_cast<uint16_t>(base_pc + 1u)));
            if (state_.carry) {
                next_pc = static_cast<uint16_t>(next_pc + rel);
            }
            break;
        }
        case 0x50: {  // JNC rel
            const int8_t rel = static_cast<int8_t>(bus_.read_code(static_cast<uint16_t>(base_pc + 1u)));
            if (!state_.carry) {
                next_pc = static_cast<uint16_t>(next_pc + rel);
            }
            break;
        }
        case 0x60: {  // JZ rel
            const int8_t rel = static_cast<int8_t>(bus_.read_code(static_cast<uint16_t>(base_pc + 1u)));
            if (state_.acc == 0u) {
                next_pc = static_cast<uint16_t>(next_pc + rel);
            }
            break;
        }
        case 0x70: {  // JNZ rel
            const int8_t rel = static_cast<int8_t>(bus_.read_code(static_cast<uint16_t>(base_pc + 1u)));
            if (state_.acc != 0u) {
                next_pc = static_cast<uint16_t>(next_pc + rel);
            }
            break;
        }
        default:
            break;
    }
    state_.psw = static_cast<uint8_t>((state_.psw & 0x7Fu) | (state_.carry ? 0x80u : 0x00u));
    pc_ = next_pc;
    return opcode_cycles(opcode);
}

uint16_t StepCpuCore::pc() const { return pc_; }

const std::array<uint64_t, 256> &StepCpuCore::opcode_histogram() const { return opcode_histogram_; }
const CpuCoreState &StepCpuCore::cpu_state() const { return state_; }

const char *TimerPeripheral::name() const { return "timer"; }

void TimerPeripheral::reset() {}

void TimerPeripheral::tick(uint64_t cycle, IBus &bus, ITraceSink &sink) {
    (void)bus;
    if (cycle != 0u && (cycle % 65536u) == 0u) {
        sink.emit(cycle, "timer_tick");
    }
}

const char *SerialPeripheral::name() const { return "serial"; }

void SerialPeripheral::reset() {}

void SerialPeripheral::tick(uint64_t cycle, IBus &bus, ITraceSink &sink) {
    (void)bus;
    if (cycle != 0u && (cycle % 65536u) == 35u) {
        sink.emit(cycle, "isr_serial_vector");
    }
}

Machine::Machine(RomImage rom) : bus_(std::move(rom)), cpu_(bus_) {}

void Machine::add_watchpoint(uint16_t pc, const std::string &tag) { watchpoints_.push_back(Watchpoint{pc, tag, false, false}); }

void Machine::add_watchpoint_once(uint16_t pc, const std::string &tag) {
    watchpoints_.push_back(Watchpoint{pc, tag, true, false});
}

void Machine::schedule_event(uint64_t cycle, const std::string &event) {
    scheduled_events_.push_back(ScheduledEvent{cycle, event});
    std::sort(
        scheduled_events_.begin(),
        scheduled_events_.end(),
        [](const ScheduledEvent &a, const ScheduledEvent &b) { return a.cycle < b.cycle; }
    );
}

void Machine::add_peripheral(std::unique_ptr<IPeripheral> peripheral) {
    peripherals_.push_back(std::move(peripheral));
}

void Machine::reset(uint16_t pc) {
    bus_.reset_runtime_state();
    cpu_.reset(pc);
    cycle_stats_ = CycleDomainStats{};
    interrupt_state_ = InterruptServiceState{};
    pending_vectors_.clear();
    cycle_ = 0u;
    scheduled_index_ = 0u;
    started_ = true;
    for (const auto &p : peripherals_) {
        p->reset();
    }
}

void Machine::begin(uint16_t pc) { reset(pc); }

uint64_t Machine::advance(uint64_t cycle_budget, ITraceSink &sink) {
    if (!started_) {
        begin(0x0000u);
    }
    const uint64_t target_cycle = cycle_ + cycle_budget;
    const uint64_t start_cycle = cycle_;
    while (cycle_ < target_cycle) {
        while (scheduled_index_ < scheduled_events_.size() && scheduled_events_[scheduled_index_].cycle <= cycle_) {
            const std::string &event = scheduled_events_[scheduled_index_].event;
            sink.emit(scheduled_events_[scheduled_index_].cycle, event);
            cycle_stats_.scheduled_emits += 1u;
            if (event.rfind("isr_", 0u) == 0u || event.find("timer1_isr") != std::string::npos) {
                cycle_stats_.interrupt_events += 1u;
                const uint16_t vec = interrupt_vector_for_event(event);
                if (vec != 0u) {
                    pending_vectors_.push_back(vec);
                    interrupt_state_.pending = static_cast<uint64_t>(pending_vectors_.size());
                    if (interrupt_state_.pending > interrupt_state_.max_pending) {
                        interrupt_state_.max_pending = interrupt_state_.pending;
                    }
                }
            }
            ++scheduled_index_;
        }
        const uint16_t pc = cpu_.pc();
        for (auto &wp : watchpoints_) {
            if (wp.pc == pc && !(wp.oneshot && wp.fired)) {
                sink.emit(cycle_, wp.tag);
                cycle_stats_.watchpoint_emits += 1u;
                if (wp.oneshot) {
                    wp.fired = true;
                }
                if (wp.tag.rfind("isr_", 0u) == 0u || wp.tag.find("timer1_isr") != std::string::npos) {
                    cycle_stats_.interrupt_events += 1u;
                    const uint16_t vec = interrupt_vector_for_event(wp.tag);
                    if (vec != 0u) {
                        pending_vectors_.push_back(vec);
                        interrupt_state_.pending = static_cast<uint64_t>(pending_vectors_.size());
                        if (interrupt_state_.pending > interrupt_state_.max_pending) {
                            interrupt_state_.max_pending = interrupt_state_.pending;
                        }
                    }
                }
            }
        }
        if (!interrupt_state_.in_service && !pending_vectors_.empty()) {
            interrupt_state_.in_service = true;
            interrupt_state_.service_steps_remaining = 4u;
            interrupt_state_.current_vector = pending_vectors_.front();
            pending_vectors_.erase(pending_vectors_.begin());
            interrupt_state_.pending = static_cast<uint64_t>(pending_vectors_.size());
            interrupt_state_.dispatches += 1u;
        }
        const uint32_t consumed = cpu_.step();
        cycle_ += static_cast<uint64_t>(consumed);
        cycle_stats_.cpu_cycles += static_cast<uint64_t>(consumed);
        if (interrupt_state_.in_service) {
            if (interrupt_state_.service_steps_remaining > consumed) {
                interrupt_state_.service_steps_remaining -= consumed;
            } else {
                interrupt_state_.service_steps_remaining = 0u;
            }
            if (interrupt_state_.service_steps_remaining == 0u) {
                interrupt_state_.in_service = false;
                interrupt_state_.serviced += 1u;
                interrupt_state_.reti_completions += 1u;
                interrupt_state_.last_vector = interrupt_state_.current_vector;
                interrupt_state_.current_vector = 0u;
            }
        }
        for (const auto &p : peripherals_) {
            p->tick(cycle_, bus_, sink);
            cycle_stats_.peripheral_ticks += 1u;
        }
    }
    return cycle_ - start_cycle;
}

uint64_t Machine::run(const RunConfig &config, ITraceSink &sink) {
    begin(config.reset_pc);
    return advance(config.max_cycles, sink);
}

uint16_t Machine::pc() const { return cpu_.pc(); }
uint64_t Machine::cycle() const { return cycle_; }

const CycleDomainStats &Machine::cycle_stats() const { return cycle_stats_; }

const std::array<uint64_t, 256> &Machine::cpu_opcode_histogram() const { return cpu_.opcode_histogram(); }
const CpuCoreState &Machine::cpu_state() const { return cpu_.cpu_state(); }
const InterruptServiceState &Machine::interrupt_state() const { return interrupt_state_; }
std::string Machine::lcd_line1() const { return bus_.lcd_line1(); }
std::string Machine::lcd_line2() const { return bus_.lcd_line2(); }

}  // namespace mmt_emu
