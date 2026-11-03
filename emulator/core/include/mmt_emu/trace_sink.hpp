#pragma once

#include <string>
#include <vector>

#include "mmt_emu/interfaces.hpp"

namespace mmt_emu {

class VectorTraceSink final : public ITraceSink {
public:
    void emit(uint64_t cycle, const std::string &event) override;
    const std::vector<TraceEvent> &events() const;

private:
    std::vector<TraceEvent> events_;
};

void write_jsonl(const std::string &path, const std::vector<TraceEvent> &events);

}  // namespace mmt_emu
