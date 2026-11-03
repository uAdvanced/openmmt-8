#include "mmt_emu/trace_sink.hpp"

#include <fstream>
#include <stdexcept>

namespace mmt_emu {

void VectorTraceSink::emit(uint64_t cycle, const std::string &event) {
    events_.push_back(TraceEvent{cycle, event});
}

const std::vector<TraceEvent> &VectorTraceSink::events() const { return events_; }

void write_jsonl(const std::string &path, const std::vector<TraceEvent> &events) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("failed to open trace output: " + path);
    }
    for (const TraceEvent &event : events) {
        out << "{\"cycle\":" << event.cycle << ",\"event\":\"" << event.event << "\"}\n";
    }
}

}  // namespace mmt_emu
