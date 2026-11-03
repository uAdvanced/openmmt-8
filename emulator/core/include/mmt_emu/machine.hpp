#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "mmt_emu/interfaces.hpp"
#include "mmt_emu/rom_image.hpp"

namespace mmt_emu {

struct RunConfig {
    uint64_t max_cycles = 0;
    uint16_t reset_pc = 0x0000;
};

struct Watchpoint {
    uint16_t pc = 0;
    std::string tag;
    bool oneshot = false;
    bool fired = false;
};

struct ScheduledEvent {
    uint64_t cycle = 0;
    std::string event;
};

struct CycleDomainStats {
    uint64_t cpu_cycles = 0;
    uint64_t scheduled_emits = 0;
    uint64_t watchpoint_emits = 0;
    uint64_t peripheral_ticks = 0;
    uint64_t interrupt_events = 0;
};

struct CpuCoreState {
    uint8_t acc = 0;
    uint8_t b = 0;
    uint8_t psw = 0;
    bool carry = false;
    uint8_t sp = 0x07;
    uint16_t dptr = 0;
    uint16_t call_depth = 0;
};

struct InterruptServiceState {
    uint64_t pending = 0;
    uint64_t serviced = 0;
    bool in_service = false;
    uint32_t service_steps_remaining = 0;
    uint64_t max_pending = 0;
    uint16_t current_vector = 0;
    uint16_t last_vector = 0;
    uint64_t dispatches = 0;
    uint64_t reti_completions = 0;
};

class FlatBus final : public IBus {
public:
    explicit FlatBus(RomImage rom);

    uint8_t read_code(uint16_t address) const override;
    uint8_t read_data(uint8_t address) const override;
    uint8_t read_xdata(uint16_t address) const override;
    void write_data(uint8_t address, uint8_t value) override;
    void write_sfr(uint8_t address, uint8_t value) override;
    void write_xdata(uint16_t address, uint8_t value) override;
    void reset_runtime_state();
    std::string lcd_line1() const;
    std::string lcd_line2() const;

private:
    void lcd_cmd(uint8_t value);
    void lcd_data(uint8_t value);
    RomImage rom_;
    uint8_t iram_[256]{};
    uint8_t sfr_[128]{};
    uint8_t xram_[65536]{};
    uint8_t lcd_ddram_[80]{};
    uint8_t lcd_addr_ = 0;
    bool lcd_entry_inc_ = true;
};

class StepCpuCore final : public ICpuCore {
public:
    explicit StepCpuCore(IBus &bus);

    void reset(uint16_t pc) override;
    uint32_t step() override;
    uint16_t pc() const override;
    const std::array<uint64_t, 256> &opcode_histogram() const;
    const CpuCoreState &cpu_state() const;

private:
    IBus &bus_;
    uint16_t pc_ = 0;
    std::array<uint64_t, 256> opcode_histogram_{};
    CpuCoreState state_{};
};

class TimerPeripheral final : public IPeripheral {
public:
    const char *name() const override;
    void reset() override;
    void tick(uint64_t cycle, IBus &bus, ITraceSink &sink) override;
};

class SerialPeripheral final : public IPeripheral {
public:
    const char *name() const override;
    void reset() override;
    void tick(uint64_t cycle, IBus &bus, ITraceSink &sink) override;
};

class Machine {
public:
    explicit Machine(RomImage rom);

    void add_watchpoint(uint16_t pc, const std::string &tag);
    void add_watchpoint_once(uint16_t pc, const std::string &tag);
    void schedule_event(uint64_t cycle, const std::string &event);
    void add_peripheral(std::unique_ptr<IPeripheral> peripheral);
    void reset(uint16_t pc);
    void begin(uint16_t pc);
    uint64_t advance(uint64_t cycle_budget, ITraceSink &sink);
    uint64_t run(const RunConfig &config, ITraceSink &sink);
    uint16_t pc() const;
    uint64_t cycle() const;
    const CycleDomainStats &cycle_stats() const;
    const std::array<uint64_t, 256> &cpu_opcode_histogram() const;
    const CpuCoreState &cpu_state() const;
    const InterruptServiceState &interrupt_state() const;
    std::string lcd_line1() const;
    std::string lcd_line2() const;

private:
    FlatBus bus_;
    StepCpuCore cpu_;
    std::vector<Watchpoint> watchpoints_;
    std::vector<ScheduledEvent> scheduled_events_;
    std::vector<uint16_t> pending_vectors_;
    std::vector<std::unique_ptr<IPeripheral>> peripherals_;
    CycleDomainStats cycle_stats_{};
    InterruptServiceState interrupt_state_{};
    uint64_t cycle_ = 0;
    size_t scheduled_index_ = 0;
    bool started_ = false;
};

}  // namespace mmt_emu
