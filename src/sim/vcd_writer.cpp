#include "asicrev/sim/vcd_writer.hpp"

#include <fmt/format.h>
#include <fmt/ostream.h>

namespace asicrev::sim {

namespace {

/// VCD identifier codes are printable ASCII in the range '!'..'~'.
std::string id_code(std::size_t n) {
    std::string s;
    do {
        s.push_back(static_cast<char>('!' + static_cast<int>(n % 94)));
        n /= 94;
    } while (n != 0);
    return s;
}

}  // namespace

VcdWriter::VcdWriter(std::ostream& out, const std::string& scope, std::uint64_t timescale_ps)
    : out_(out), scope_(scope), timescale_ps_(timescale_ps) {}

void VcdWriter::add_signal(const std::string& name) {
    if (ids_.contains(name)) {
        return;
    }
    ids_.emplace(name, id_code(names_.size()));
    names_.push_back(name);
}

void VcdWriter::finish_header() {
    fmt::print(out_, "$timescale {}ps $end\n", timescale_ps_);
    fmt::print(out_, "$scope module {} $end\n", scope_);
    for (const std::string& n : names_) {
        fmt::print(out_, "$var wire 1 {} {} $end\n", ids_.at(n), n);
    }
    out_ << "$upscope $end\n$enddefinitions $end\n";
    header_done_ = true;

    out_ << "$dumpvars\n";
    for (const std::string& n : names_) {
        fmt::print(out_, "x{}\n", ids_.at(n));
        last_[n] = Logic::Unknown;
    }
    out_ << "$end\n";
    time_emitted_ = true;
}

void VcdWriter::set(const std::string& name, Logic value) {
    if (!ids_.contains(name)) {
        return;
    }
    pending_[name] = value;
}

void VcdWriter::flush_time() {
    bool any = false;
    for (const std::string& n : names_) {
        const auto it = pending_.find(n);
        if (it == pending_.end()) {
            continue;
        }
        const auto prev = last_.find(n);
        if (prev != last_.end() && prev->second == it->second) {
            continue;
        }
        if (!any) {
            fmt::print(out_, "#{}\n", time_);
            any = true;
        }
        fmt::print(out_, "{}{}\n", to_char(it->second), ids_.at(n));
        last_[n] = it->second;
    }
    pending_.clear();
}

void VcdWriter::advance_to(std::uint64_t time) {
    flush_time();
    time_ = time;
}

}  // namespace asicrev::sim
