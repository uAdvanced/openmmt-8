#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mmt_emu {

struct TraceEvent {
    uint64_t cycle;
    std::string event;
};

class ITraceSink {
public:
    virtual ~ITraceSink() = default;
    virtual void emit(uint64_t cycle, const std::string &event) = 0;
};

class IRomImage {
public:
    virtual ~IRomImage() = default;
    virtual const std::vector<uint8_t> &bytes() const = 0;
    virtual size_t size() const = 0;
};

class IBus {
public:
    virtual ~IBus() = default;
    virtual uint8_t read_code(uint16_t address) const = 0;
    virtual uint8_t read_data(uint8_t address) const = 0;
    virtual uint8_t read_xdata(uint16_t address) const = 0;
    virtual void write_data(uint8_t address, uint8_t value) = 0;
    virtual void write_sfr(uint8_t address, uint8_t value) = 0;
    virtual void write_xdata(uint16_t address, uint8_t value) = 0;
};

class ICpuCore {
public:
    virtual ~ICpuCore() = default;
    virtual void reset(uint16_t pc) = 0;
    virtual uint32_t step() = 0;
    virtual uint16_t pc() const = 0;
};

class IPeripheral {
public:
    virtual ~IPeripheral() = default;
    virtual const char *name() const = 0;
    virtual void reset() = 0;
    virtual void tick(uint64_t cycle, IBus &bus, ITraceSink &sink) = 0;
};

}  // namespace mmt_emu
