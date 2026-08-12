#pragma once

#include "asicrev/netlist/netlist.hpp"
#include "asicrev/tech/std_cells.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace asicrev::sim {

enum class Logic : std::uint8_t { Zero = 0, One = 1, Unknown = 2 };

inline char to_char(Logic v) {
    switch (v) {
        case Logic::Zero: return '0';
        case Logic::One: return '1';
        default: return 'x';
    }
}

/// Levelised zero-delay simulator for a gate-level netlist.
///
/// The combinational cloud is topologically sorted once at construction;
/// sequential cells (flip-flops) are the level boundary, exactly like a normal
/// cycle-based simulator. Combinational loops are reported at build time.
class Simulator {
public:
    Simulator(const netlist::Netlist& nl, const tech::CellLibrary& cells);

    /// Nets that no cell drives, minus the power rails: the primary inputs.
    const std::vector<std::size_t>& free_inputs() const { return free_inputs_; }

    const std::vector<std::string>& warnings() const { return warnings_; }

    void set_net(std::size_t net, Logic value);
    void set_net(const std::string& name, Logic value);
    Logic get_net(std::size_t net) const;
    Logic get_net(const std::string& name) const;

    /// Re-evaluate the whole combinational cloud from the current net state.
    void settle();

    /// One rising clock edge on `clock_net`: sample D, then update Q and settle.
    /// Asynchronous resets are applied continuously by `settle()`.
    void clock_edge(std::size_t clock_net);
    void clock_edge(const std::string& clock_name);

    /// Drive every net to Unknown and re-apply constants.
    void reset_state();

    std::size_t flip_flop_count() const { return flops_.size(); }

private:
    struct Eval {
        std::size_t instance;
        const tech::CellModel* model;
    };

    struct Flop {
        std::size_t instance;
        const tech::CellModel* model;
        std::size_t clk = netlist::kNoNet;
        std::size_t d = netlist::kNoNet;
        std::size_t q = netlist::kNoNet;
        std::size_t qn = netlist::kNoNet;
        std::size_t reset = netlist::kNoNet;
        bool reset_active_low = true;
        std::size_t set = netlist::kNoNet;
        bool set_active_low = true;
        Logic state = Logic::Unknown;
    };

    void build_levels();
    void apply_async(Flop& f);
    void evaluate(const Eval& e);
    Logic eval_expr(const tech::Expr& e, const netlist::Instance& inst) const;

    const netlist::Netlist& nl_;
    const tech::CellLibrary& cells_;
    std::vector<Logic> values_;
    std::vector<Eval> order_;
    std::vector<Flop> flops_;
    std::vector<std::size_t> free_inputs_;
    std::vector<std::string> warnings_;
};

}  // namespace asicrev::sim
