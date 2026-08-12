#include "asicrev/tech/std_cells.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace asicrev::tech {

// ------------------------------------------------------------------ Expr

std::shared_ptr<const Expr> Expr::var_ref(std::string name) {
    auto e = std::make_shared<Expr>();
    e->op = Op::Var;
    e->var = std::move(name);
    return e;
}

std::shared_ptr<const Expr> Expr::constant(bool v) {
    auto e = std::make_shared<Expr>();
    e->op = Op::Const;
    e->value = v;
    return e;
}

std::shared_ptr<const Expr> Expr::negate(std::shared_ptr<const Expr> a) {
    auto e = std::make_shared<Expr>();
    e->op = Op::Not;
    e->args.push_back(std::move(a));
    return e;
}

std::shared_ptr<const Expr> Expr::conj(std::vector<std::shared_ptr<const Expr>> a) {
    auto e = std::make_shared<Expr>();
    e->op = Op::And;
    e->args = std::move(a);
    return e;
}

std::shared_ptr<const Expr> Expr::disj(std::vector<std::shared_ptr<const Expr>> a) {
    auto e = std::make_shared<Expr>();
    e->op = Op::Or;
    e->args = std::move(a);
    return e;
}

std::shared_ptr<const Expr> Expr::exor(std::vector<std::shared_ptr<const Expr>> a) {
    auto e = std::make_shared<Expr>();
    e->op = Op::Xor;
    e->args = std::move(a);
    return e;
}

namespace {

ExprPtr v(const char* name) {
    return Expr::var_ref(name);
}

ExprPtr nt(ExprPtr a) {
    return Expr::negate(std::move(a));
}

ExprPtr and_(std::vector<ExprPtr> a) {
    return Expr::conj(std::move(a));
}

ExprPtr or_(std::vector<ExprPtr> a) {
    return Expr::disj(std::move(a));
}

ExprPtr xor_(std::vector<ExprPtr> a) {
    return Expr::exor(std::move(a));
}

/// Standard power/body pins shared by every sky130_fd_sc_hd cell.
std::vector<Pin> with_supplies(std::vector<Pin> signal_pins) {
    signal_pins.push_back(Pin{"VPWR", PinDirection::Power});
    signal_pins.push_back(Pin{"VGND", PinDirection::Ground});
    signal_pins.push_back(Pin{"VPB", PinDirection::Body});
    signal_pins.push_back(Pin{"VNB", PinDirection::Body});
    return signal_pins;
}

Pin in(const char* n) {
    return Pin{n, PinDirection::Input};
}

Pin out(const char* n) {
    return Pin{n, PinDirection::Output};
}

/// One combinational cell, replicated across its drive-strength variants.
struct CombSpec {
    const char* base;
    std::vector<Pin> signal_pins;
    const char* out_pin;
    ExprPtr func;
};

const std::vector<int>& drive_strengths() {
    static const std::vector<int> ds = {1, 2, 4, 6, 8, 12, 16};
    return ds;
}

std::string render_expr(const Expr& e) {
    switch (e.op) {
        case Expr::Op::Var: return e.var;
        case Expr::Op::Const: return e.value ? "1'b1" : "1'b0";
        case Expr::Op::Not: return fmt::format("(~{})", render_expr(*e.args[0]));
        case Expr::Op::And:
        case Expr::Op::Or:
        case Expr::Op::Xor: {
            const char* sep =
                e.op == Expr::Op::And ? " & " : (e.op == Expr::Op::Or ? " | " : " ^ ");
            std::string s = "(";
            for (std::size_t i = 0; i < e.args.size(); ++i) {
                if (i != 0) {
                    s += sep;
                }
                s += render_expr(*e.args[i]);
            }
            return s + ")";
        }
    }
    return "1'bx";
}

}  // namespace

// ------------------------------------------------------------- CellModel

const Pin* CellModel::find_pin(std::string_view pin_name) const {
    for (const Pin& p : pins) {
        if (p.name == pin_name) {
            return &p;
        }
    }
    return nullptr;
}

std::vector<std::string> CellModel::input_pins() const {
    std::vector<std::string> r;
    for (const Pin& p : pins) {
        if (p.direction == PinDirection::Input) {
            r.push_back(p.name);
        }
    }
    return r;
}

std::vector<std::string> CellModel::output_pins() const {
    std::vector<std::string> r;
    for (const Pin& p : pins) {
        if (p.direction == PinDirection::Output) {
            r.push_back(p.name);
        }
    }
    return r;
}

// ----------------------------------------------------------- CellLibrary

void CellLibrary::add(CellModel model) {
    cells_.emplace(model.name, std::move(model));
}

const CellModel* CellLibrary::find(std::string_view cell_name) const {
    const auto it = cells_.find(std::string(cell_name));
    return it == cells_.end() ? nullptr : &it->second;
}

std::vector<std::string> CellLibrary::names() const {
    std::vector<std::string> r;
    r.reserve(cells_.size());
    for (const auto& [name, _] : cells_) {
        r.push_back(name);
    }
    std::sort(r.begin(), r.end());
    return r;
}

std::string base_cell_name(std::string_view full_name) {
    const auto pos = full_name.rfind("__");
    std::string_view leaf = pos == std::string_view::npos ? full_name : full_name.substr(pos + 2);
    // Strip a trailing _<digits> drive strength suffix.
    auto end = leaf.size();
    std::size_t digits = 0;
    while (end > 0 && (std::isdigit(static_cast<unsigned char>(leaf[end - 1])) != 0)) {
        --end;
        ++digits;
    }
    if (digits > 0 && end > 0 && leaf[end - 1] == '_') {
        leaf = leaf.substr(0, end - 1);
    }
    return std::string(leaf);
}

const CellLibrary& CellLibrary::sky130_hd() {
    static const CellLibrary lib = [] {
        CellLibrary l;
        const std::string prefix = "sky130_fd_sc_hd__";

        // Combinational cells. Every function below was transcribed from the
        // SkyWater PDK `cells/<base>/sky130_fd_sc_hd__<base>.functional.pp.v`
        // gate-level model (tests/test_std_cells.cpp re-checks the truth
        // tables against those models).
        const std::vector<CombSpec> comb = {
            {"buf", {in("A"), out("X")}, "X", v("A")},
            {"bufbuf", {in("A"), out("X")}, "X", v("A")},
            {"clkbuf", {in("A"), out("X")}, "X", v("A")},
            {"dlygate4sd3", {in("A"), out("X")}, "X", v("A")},
            {"inv", {in("A"), out("Y")}, "Y", nt(v("A"))},
            {"clkinv", {in("A"), out("Y")}, "Y", nt(v("A"))},
            {"einvp", {in("A"), in("TE"), out("Z")}, "Z", nt(v("A"))},

            {"and2", {in("A"), in("B"), out("X")}, "X", and_({v("A"), v("B")})},
            {"and2b", {in("A_N"), in("B"), out("X")}, "X", and_({nt(v("A_N")), v("B")})},
            {"and3", {in("A"), in("B"), in("C"), out("X")}, "X", and_({v("A"), v("B"), v("C")})},
            {"and3b",
             {in("A_N"), in("B"), in("C"), out("X")},
             "X",
             and_({nt(v("A_N")), v("B"), v("C")})},
            {"and4",
             {in("A"), in("B"), in("C"), in("D"), out("X")},
             "X",
             and_({v("A"), v("B"), v("C"), v("D")})},
            {"and4b",
             {in("A_N"), in("B"), in("C"), in("D"), out("X")},
             "X",
             and_({nt(v("A_N")), v("B"), v("C"), v("D")})},
            {"and4bb",
             {in("A_N"), in("B_N"), in("C"), in("D"), out("X")},
             "X",
             and_({nt(or_({v("A_N"), v("B_N")})), v("C"), v("D")})},

            {"nand3b",
             {in("A_N"), in("B"), in("C"), out("Y")},
             "Y",
             nt(and_({nt(v("A_N")), v("B"), v("C")}))},

            {"or2", {in("A"), in("B"), out("X")}, "X", or_({v("A"), v("B")})},
            {"or2b", {in("A"), in("B_N"), out("X")}, "X", or_({v("A"), nt(v("B_N"))})},
            {"or3", {in("A"), in("B"), in("C"), out("X")}, "X", or_({v("A"), v("B"), v("C")})},
            {"or3b",
             {in("A"), in("B"), in("C_N"), out("X")},
             "X",
             or_({v("A"), v("B"), nt(v("C_N"))})},
            {"or4",
             {in("A"), in("B"), in("C"), in("D"), out("X")},
             "X",
             or_({v("A"), v("B"), v("C"), v("D")})},
            {"or4b",
             {in("A"), in("B"), in("C"), in("D_N"), out("X")},
             "X",
             or_({v("A"), v("B"), v("C"), nt(v("D_N"))})},
            {"or4bb",
             {in("A"), in("B"), in("C_N"), in("D_N"), out("X")},
             "X",
             or_({v("A"), v("B"), nt(and_({v("C_N"), v("D_N")}))})},

            {"nand2", {in("A"), in("B"), out("Y")}, "Y", nt(and_({v("A"), v("B")}))},
            {"nand2b", {in("A_N"), in("B"), out("Y")}, "Y", nt(and_({nt(v("A_N")), v("B")}))},
            {"nand3",
             {in("A"), in("B"), in("C"), out("Y")},
             "Y",
             nt(and_({v("A"), v("B"), v("C")}))},
            {"nand4",
             {in("A"), in("B"), in("C"), in("D"), out("Y")},
             "Y",
             nt(and_({v("A"), v("B"), v("C"), v("D")}))},

            {"nor2", {in("A"), in("B"), out("Y")}, "Y", nt(or_({v("A"), v("B")}))},
            {"nor2b", {in("A"), in("B_N"), out("Y")}, "Y", nt(or_({v("A"), nt(v("B_N"))}))},
            {"nor3", {in("A"), in("B"), in("C"), out("Y")}, "Y", nt(or_({v("A"), v("B"), v("C")}))},
            {"nor3b",
             {in("A"), in("B"), in("C_N"), out("Y")},
             "Y",
             and_({v("C_N"), nt(or_({v("A"), v("B")}))})},
            {"nor4",
             {in("A"), in("B"), in("C"), in("D"), out("Y")},
             "Y",
             nt(or_({v("A"), v("B"), v("C"), v("D")}))},
            {"nor4b",
             {in("A"), in("B"), in("C"), in("D_N"), out("Y")},
             "Y",
             nt(or_({v("A"), v("B"), v("C"), nt(v("D_N"))}))},

            {"xor2", {in("A"), in("B"), out("X")}, "X", xor_({v("A"), v("B")})},
            {"xor3", {in("A"), in("B"), in("C"), out("X")}, "X", xor_({v("A"), v("B"), v("C")})},
            {"xnor2", {in("A"), in("B"), out("Y")}, "Y", nt(xor_({v("A"), v("B")}))},
            {"xnor3",
             {in("A"), in("B"), in("C"), out("Y")},
             "Y",
             nt(xor_({v("A"), v("B"), v("C")}))},

            // AND-OR / OR-AND compounds. Naming: aXY[b][i] = X-input AND into
            // the first input of a Y-input OR; `b` marks an inverted B input,
            // `i` an inverted output.
            {"a21o",
             {in("A1"), in("A2"), in("B1"), out("X")},
             "X",
             or_({and_({v("A1"), v("A2")}), v("B1")})},
            {"a21oi",
             {in("A1"), in("A2"), in("B1"), out("Y")},
             "Y",
             nt(or_({and_({v("A1"), v("A2")}), v("B1")}))},
            {"a21bo",
             {in("A1"), in("A2"), in("B1_N"), out("X")},
             "X",
             or_({and_({v("A1"), v("A2")}), nt(v("B1_N"))})},
            {"a21boi",
             {in("A1"), in("A2"), in("B1_N"), out("Y")},
             "Y",
             nt(or_({and_({v("A1"), v("A2")}), nt(v("B1_N"))}))},
            {"a22o",
             {in("A1"), in("A2"), in("B1"), in("B2"), out("X")},
             "X",
             or_({and_({v("A1"), v("A2")}), and_({v("B1"), v("B2")})})},
            {"a22oi",
             {in("A1"), in("A2"), in("B1"), in("B2"), out("Y")},
             "Y",
             nt(or_({and_({v("A1"), v("A2")}), and_({v("B1"), v("B2")})}))},
            {"a31o",
             {in("A1"), in("A2"), in("A3"), in("B1"), out("X")},
             "X",
             or_({and_({v("A1"), v("A2"), v("A3")}), v("B1")})},
            {"a31oi",
             {in("A1"), in("A2"), in("A3"), in("B1"), out("Y")},
             "Y",
             nt(or_({and_({v("A1"), v("A2"), v("A3")}), v("B1")}))},
            {"a32o",
             {in("A1"), in("A2"), in("A3"), in("B1"), in("B2"), out("X")},
             "X",
             or_({and_({v("A1"), v("A2"), v("A3")}), and_({v("B1"), v("B2")})})},
            {"a41o",
             {in("A1"), in("A2"), in("A3"), in("A4"), in("B1"), out("X")},
             "X",
             or_({and_({v("A1"), v("A2"), v("A3"), v("A4")}), v("B1")})},
            {"a211o",
             {in("A1"), in("A2"), in("B1"), in("C1"), out("X")},
             "X",
             or_({and_({v("A1"), v("A2")}), v("B1"), v("C1")})},
            {"a211oi",
             {in("A1"), in("A2"), in("B1"), in("C1"), out("Y")},
             "Y",
             nt(or_({and_({v("A1"), v("A2")}), v("B1"), v("C1")}))},
            {"a221o",
             {in("A1"), in("A2"), in("B1"), in("B2"), in("C1"), out("X")},
             "X",
             or_({and_({v("A1"), v("A2")}), and_({v("B1"), v("B2")}), v("C1")})},
            {"a222oi",
             {in("A1"), in("A2"), in("B1"), in("B2"), in("C1"), in("C2"), out("Y")},
             "Y",
             nt(or_(
                 {and_({v("A1"), v("A2")}), and_({v("B1"), v("B2")}), and_({v("C1"), v("C2")})}))},
            {"a311o",
             {in("A1"), in("A2"), in("A3"), in("B1"), in("C1"), out("X")},
             "X",
             or_({and_({v("A1"), v("A2"), v("A3")}), v("B1"), v("C1")})},
            {"a2111o",
             {in("A1"), in("A2"), in("B1"), in("C1"), in("D1"), out("X")},
             "X",
             or_({and_({v("A1"), v("A2")}), v("B1"), v("C1"), v("D1")})},
            {"a2111oi",
             {in("A1"), in("A2"), in("B1"), in("C1"), in("D1"), out("Y")},
             "Y",
             nt(or_({and_({v("A1"), v("A2")}), v("B1"), v("C1"), v("D1")}))},
            {"a221oi",
             {in("A1"), in("A2"), in("B1"), in("B2"), in("C1"), out("Y")},
             "Y",
             nt(or_({and_({v("A1"), v("A2")}), and_({v("B1"), v("B2")}), v("C1")}))},
            {"a41oi",
             {in("A1"), in("A2"), in("A3"), in("A4"), in("B1"), out("Y")},
             "Y",
             nt(or_({and_({v("A1"), v("A2"), v("A3"), v("A4")}), v("B1")}))},

            {"o21a",
             {in("A1"), in("A2"), in("B1"), out("X")},
             "X",
             and_({or_({v("A1"), v("A2")}), v("B1")})},
            {"o21ai",
             {in("A1"), in("A2"), in("B1"), out("Y")},
             "Y",
             nt(and_({or_({v("A1"), v("A2")}), v("B1")}))},
            {"o21ba",
             {in("A1"), in("A2"), in("B1_N"), out("X")},
             "X",
             and_({or_({v("A1"), v("A2")}), nt(v("B1_N"))})},
            {"o21bai",
             {in("A1"), in("A2"), in("B1_N"), out("Y")},
             "Y",
             nt(and_({or_({v("A1"), v("A2")}), nt(v("B1_N"))}))},
            {"o22a",
             {in("A1"), in("A2"), in("B1"), in("B2"), out("X")},
             "X",
             and_({or_({v("A1"), v("A2")}), or_({v("B1"), v("B2")})})},
            {"o22ai",
             {in("A1"), in("A2"), in("B1"), in("B2"), out("Y")},
             "Y",
             nt(and_({or_({v("A1"), v("A2")}), or_({v("B1"), v("B2")})}))},
            {"o31a",
             {in("A1"), in("A2"), in("A3"), in("B1"), out("X")},
             "X",
             and_({or_({v("A1"), v("A2"), v("A3")}), v("B1")})},
            {"o31ai",
             {in("A1"), in("A2"), in("A3"), in("B1"), out("Y")},
             "Y",
             nt(and_({or_({v("A1"), v("A2"), v("A3")}), v("B1")}))},
            {"o32a",
             {in("A1"), in("A2"), in("A3"), in("B1"), in("B2"), out("X")},
             "X",
             and_({or_({v("A1"), v("A2"), v("A3")}), or_({v("B1"), v("B2")})})},
            {"o32ai",
             {in("A1"), in("A2"), in("A3"), in("B1"), in("B2"), out("Y")},
             "Y",
             nt(and_({or_({v("A1"), v("A2"), v("A3")}), or_({v("B1"), v("B2")})}))},
            {"o41a",
             {in("A1"), in("A2"), in("A3"), in("A4"), in("B1"), out("X")},
             "X",
             and_({or_({v("A1"), v("A2"), v("A3"), v("A4")}), v("B1")})},
            // o2bb2a: X = ~(A1_N & A2_N) & (B1 | B2)
            {"o2bb2a",
             {in("A1_N"), in("A2_N"), in("B1"), in("B2"), out("X")},
             "X",
             and_({nt(and_({v("A1_N"), v("A2_N")})), or_({v("B1"), v("B2")})})},
            {"o2bb2ai",
             {in("A1_N"), in("A2_N"), in("B1"), in("B2"), out("Y")},
             "Y",
             nt(and_({nt(and_({v("A1_N"), v("A2_N")})), or_({v("B1"), v("B2")})}))},
            {"o211a",
             {in("A1"), in("A2"), in("B1"), in("C1"), out("X")},
             "X",
             and_({or_({v("A1"), v("A2")}), v("B1"), v("C1")})},
            {"o211ai",
             {in("A1"), in("A2"), in("B1"), in("C1"), out("Y")},
             "Y",
             nt(and_({or_({v("A1"), v("A2")}), v("B1"), v("C1")}))},
            {"o221a",
             {in("A1"), in("A2"), in("B1"), in("B2"), in("C1"), out("X")},
             "X",
             and_({or_({v("A1"), v("A2")}), or_({v("B1"), v("B2")}), v("C1")})},
            {"o311a",
             {in("A1"), in("A2"), in("A3"), in("B1"), in("C1"), out("X")},
             "X",
             and_({or_({v("A1"), v("A2"), v("A3")}), v("B1"), v("C1")})},

            // Majority / minority.
            {"maj3",
             {in("A"), in("B"), in("C"), out("X")},
             "X",
             or_({and_({v("A"), v("B")}), and_({v("B"), v("C")}), and_({v("A"), v("C")})})},

            // Muxes: X = S ? A1 : A0.
            {"mux2",
             {in("A0"), in("A1"), in("S"), out("X")},
             "X",
             or_({and_({v("A0"), nt(v("S"))}), and_({v("A1"), v("S")})})},
            {"mux2i",
             {in("A0"), in("A1"), in("S"), out("Y")},
             "Y",
             nt(or_({and_({v("A0"), nt(v("S"))}), and_({v("A1"), v("S")})}))},
            {"mux4",
             {in("A0"), in("A1"), in("A2"), in("A3"), in("S0"), in("S1"), out("X")},
             "X",
             or_({and_({v("A0"), nt(v("S0")), nt(v("S1"))}), and_({v("A1"), v("S0"), nt(v("S1"))}),
                  and_({v("A2"), nt(v("S0")), v("S1")}), and_({v("A3"), v("S0"), v("S1")})})},

            // Tie cells.
            {"conb", {out("HI"), out("LO")}, "HI", Expr::constant(true)},
        };

        for (const CombSpec& spec : comb) {
            for (int ds : drive_strengths()) {
                CellModel m;
                m.name = fmt::format("{}{}_{}", prefix, spec.base, ds);
                m.kind = CellKind::Combinational;
                m.pins = with_supplies(spec.signal_pins);
                m.functions.emplace_back(spec.out_pin, spec.func);
                if (std::string(spec.base) == "conb") {
                    m.functions.emplace_back("LO", Expr::constant(false));
                }
                l.add(std::move(m));
            }
        }

        // Flip-flops. dfrtp: rising edge, active-low async reset, Q only.
        struct FfSpec {
            const char* base;
            bool has_reset;
            bool has_set;
            bool qn;
            bool posedge;
        };
        const std::vector<FfSpec> ffs = {
            {"dfxtp", false, false, false, true}, {"dfxbp", false, false, true, true},
            {"dfrtp", true, false, false, true},  {"dfrbp", true, false, true, true},
            {"dfstp", false, true, false, true},  {"dfsbp", false, true, true, true},
            {"dfrtn", true, false, false, false},
        };
        for (const FfSpec& spec : ffs) {
            for (int ds : drive_strengths()) {
                CellModel m;
                m.name = fmt::format("{}{}_{}", prefix, spec.base, ds);
                m.kind = CellKind::Sequential;
                std::vector<Pin> pins = {in("CLK"), in("D"), out("Q")};
                if (spec.qn) {
                    pins.push_back(out("Q_N"));
                }
                if (spec.has_reset) {
                    pins.push_back(in("RESET_B"));
                }
                if (spec.has_set) {
                    pins.push_back(in("SET_B"));
                }
                m.pins = with_supplies(std::move(pins));
                m.seq.clock_pin = "CLK";
                m.seq.clock_posedge = spec.posedge;
                m.seq.data_pin = "D";
                m.seq.q_pin = "Q";
                m.seq.qn_pin = spec.qn ? "Q_N" : "";
                m.seq.reset_pin = spec.has_reset ? "RESET_B" : "";
                m.seq.reset_active_low = true;
                m.seq.set_pin = spec.has_set ? "SET_B" : "";
                m.seq.set_active_low = true;
                l.add(std::move(m));
            }
        }

        // Physical-only cells: no signal pins, no behaviour.
        const std::vector<std::string> physical = {
            "sky130_fd_sc_hd__decap_3",      "sky130_fd_sc_hd__decap_4",
            "sky130_fd_sc_hd__decap_6",      "sky130_fd_sc_hd__decap_8",
            "sky130_fd_sc_hd__decap_12",     "sky130_fd_sc_hd__fill_1",
            "sky130_fd_sc_hd__fill_2",       "sky130_fd_sc_hd__fill_4",
            "sky130_fd_sc_hd__fill_8",       "sky130_fd_sc_hd__tap_1",
            "sky130_fd_sc_hd__tap_2",        "sky130_fd_sc_hd__diode_2",
            "sky130_fd_sc_hd__fakediode_2",  "sky130_ef_sc_hd__decap_12",
            "sky130_fd_sc_hd__fill_diode_2",
        };
        for (const std::string& name : physical) {
            CellModel m;
            m.name = name;
            m.kind = CellKind::Physical;
            m.pins = with_supplies({});
            l.add(std::move(m));
        }

        // The well tap ties the wells to the rails internally, so the flow
        // instantiates it with VPWR/VGND only - it has no separate body pins to
        // connect, unlike every other cell in the library.
        {
            CellModel tap;
            tap.name = "sky130_fd_sc_hd__tapvpwrvgnd_1";
            tap.kind = CellKind::Physical;
            tap.pins = {Pin{"VPWR", PinDirection::Power}, Pin{"VGND", PinDirection::Ground}};
            l.add(std::move(tap));
        }

        return l;
    }();
    return lib;
}

std::string CellLibrary::verilog_stubs(const std::vector<std::string>& only) const {
    std::vector<std::string> wanted = only.empty() ? names() : only;
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());

    std::ostringstream os;
    os << "// Behavioural stubs for the sky130_fd_sc_hd cells used by this netlist.\n"
       << "// Generated by asicrev; power pins are accepted and ignored.\n"
       << "`timescale 1ns / 1ps\n\n";

    for (const std::string& name : wanted) {
        const CellModel* m = find(name);
        if (m == nullptr) {
            os << fmt::format("// WARNING: no model for {}\n", name);
            continue;
        }

        std::vector<std::string> port_names;
        for (const Pin& p : m->pins) {
            port_names.push_back(p.name);
        }
        os << fmt::format("module {} ({});\n", m->name, fmt::join(port_names, ", "));
        for (const Pin& p : m->pins) {
            const char* dir = p.direction == PinDirection::Output ? "output" : "input";
            os << fmt::format("    {} {};\n", dir, p.name);
        }

        if (m->kind == CellKind::Combinational) {
            for (const auto& [pin, fn] : m->functions) {
                os << fmt::format("    assign {} = {};\n", pin, render_expr(*fn));
            }
        } else if (m->kind == CellKind::Sequential) {
            const SequentialInfo& s = m->seq;
            os << "    reg q_state;\n";
            const char* edge = s.clock_posedge ? "posedge" : "negedge";
            std::string sensitivity = fmt::format("{} {}", edge, s.clock_pin);
            if (!s.reset_pin.empty()) {
                sensitivity += fmt::format(" or {} {}", s.reset_active_low ? "negedge" : "posedge",
                                           s.reset_pin);
            }
            if (!s.set_pin.empty()) {
                sensitivity +=
                    fmt::format(" or {} {}", s.set_active_low ? "negedge" : "posedge", s.set_pin);
            }
            os << fmt::format("    always @({}) begin\n", sensitivity);
            std::string indent = "        ";
            if (!s.reset_pin.empty()) {
                os << fmt::format("{}if ({}{}) q_state <= 1'b0;\n", indent,
                                  s.reset_active_low ? "!" : "", s.reset_pin);
                indent = "        else ";
            }
            if (!s.set_pin.empty()) {
                os << fmt::format("{}if ({}{}) q_state <= 1'b1;\n", indent,
                                  s.set_active_low ? "!" : "", s.set_pin);
                indent = "        else ";
            }
            os << fmt::format("{}q_state <= {};\n", indent, s.data_pin);
            os << "    end\n";
            os << fmt::format("    assign {} = q_state;\n", s.q_pin);
            if (!s.qn_pin.empty()) {
                os << fmt::format("    assign {} = ~q_state;\n", s.qn_pin);
            }
        }

        os << "endmodule\n\n";
    }
    return os.str();
}

}  // namespace asicrev::tech
