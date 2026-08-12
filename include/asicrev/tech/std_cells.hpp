#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace asicrev::tech {

enum class PinDirection : std::uint8_t { Input, Output, Power, Ground, Body };

struct Pin {
    std::string name;
    PinDirection direction = PinDirection::Input;
};

enum class CellKind : std::uint8_t {
    Combinational,  ///< output described by `function`
    Sequential,     ///< D flip-flop, see `seq`
    Physical,       ///< decap / tap / fill: no signal pins at all
};

/// Sequential behaviour of the flip-flop family we support.
struct SequentialInfo {
    std::string clock_pin;
    bool clock_posedge = true;
    std::string data_pin;
    std::string q_pin;
    std::string qn_pin;     ///< empty when the cell has no inverted output
    std::string reset_pin;  ///< async clear, empty when absent
    bool reset_active_low = true;
    std::string set_pin;  ///< async preset, empty when absent
    bool set_active_low = true;
};

/// Boolean expression AST over pin names. Kept tiny on purpose: it is both the
/// simulator's evaluation form and the source for the generated Verilog stubs.
struct Expr {
    enum class Op : std::uint8_t { Var, Const, Not, And, Or, Xor };

    Op op = Op::Const;
    std::string var;     ///< for Op::Var
    bool value = false;  ///< for Op::Const
    std::vector<std::shared_ptr<const Expr>> args;

    static std::shared_ptr<const Expr> var_ref(std::string name);
    static std::shared_ptr<const Expr> constant(bool v);
    static std::shared_ptr<const Expr> negate(std::shared_ptr<const Expr> a);
    static std::shared_ptr<const Expr> conj(std::vector<std::shared_ptr<const Expr>> a);
    static std::shared_ptr<const Expr> disj(std::vector<std::shared_ptr<const Expr>> a);
    static std::shared_ptr<const Expr> exor(std::vector<std::shared_ptr<const Expr>> a);
};

using ExprPtr = std::shared_ptr<const Expr>;

struct CellModel {
    std::string name;  ///< full library name, e.g. sky130_fd_sc_hd__nand2_2
    CellKind kind = CellKind::Combinational;
    std::vector<Pin> pins;
    /// Output pin name -> driving function, for CellKind::Combinational.
    std::vector<std::pair<std::string, ExprPtr>> functions;
    SequentialInfo seq;

    const Pin* find_pin(std::string_view name) const;
    std::vector<std::string> input_pins() const;
    std::vector<std::string> output_pins() const;

    bool is_physical() const { return kind == CellKind::Physical; }
};

/// Library of standard cell behaviours.
class CellLibrary {
public:
    /// The sky130_fd_sc_hd cells needed by the Jane Street puzzles. Functions
    /// were transcribed from the SkyWater PDK `*.functional.pp.v` models.
    static const CellLibrary& sky130_hd();

    const CellModel* find(std::string_view cell_name) const;
    std::vector<std::string> names() const;

    /// Render Verilog behavioural stubs for the given cells (or all of them
    /// when `only` is empty) so iverilog / yosys can consume our netlist
    /// without the full PDK installed.
    std::string verilog_stubs(const std::vector<std::string>& only = {}) const;

private:
    void add(CellModel model);

    std::unordered_map<std::string, CellModel> cells_;
};

/// Strip the drive-strength suffix: sky130_fd_sc_hd__nand2_2 -> nand2.
std::string base_cell_name(std::string_view full_name);

}  // namespace asicrev::tech
