#pragma once

#include "asicrev/sim/simulator.hpp"

#include <cstdint>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace asicrev::sim {

/// Minimal VCD writer: enough for Surfer / GTKWave to show the trace.
class VcdWriter {
public:
    VcdWriter(std::ostream& out, const std::string& scope, std::uint64_t timescale_ps = 1000);

    /// Register a scalar signal before `finish_header()`.
    void add_signal(const std::string& name);

    void finish_header();

    /// Record `value` for `name` at the current time; only changes are emitted.
    void set(const std::string& name, Logic value);

    void advance_to(std::uint64_t time);

    void flush_time();

private:
    std::ostream& out_;
    std::string scope_;
    std::uint64_t timescale_ps_;
    std::vector<std::string> names_;
    std::unordered_map<std::string, std::string> ids_;
    std::unordered_map<std::string, Logic> last_;
    std::unordered_map<std::string, Logic> pending_;
    std::uint64_t time_ = 0;
    bool header_done_ = false;
    bool time_emitted_ = false;
};

}  // namespace asicrev::sim
