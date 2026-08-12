#include "asicrev/sim/simulator.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace asicrev::sim {

namespace {

Logic logic_and(Logic a, Logic b) {
    if (a == Logic::Zero || b == Logic::Zero) {
        return Logic::Zero;
    }
    if (a == Logic::Unknown || b == Logic::Unknown) {
        return Logic::Unknown;
    }
    return Logic::One;
}

Logic logic_or(Logic a, Logic b) {
    if (a == Logic::One || b == Logic::One) {
        return Logic::One;
    }
    if (a == Logic::Unknown || b == Logic::Unknown) {
        return Logic::Unknown;
    }
    return Logic::Zero;
}

Logic logic_xor(Logic a, Logic b) {
    if (a == Logic::Unknown || b == Logic::Unknown) {
        return Logic::Unknown;
    }
    return a == b ? Logic::Zero : Logic::One;
}

Logic logic_not(Logic a) {
    switch (a) {
        case Logic::Zero: return Logic::One;
        case Logic::One: return Logic::Zero;
        default: return Logic::Unknown;
    }
}

}  // namespace

Simulator::Simulator(const netlist::Netlist& nl, const tech::CellLibrary& cells)
    : nl_(nl), cells_(cells), values_(nl.nets.size(), Logic::Unknown) {
    build_levels();
    reset_state();
}

void Simulator::build_levels() {
    // Which instance drives each net, and which instances read it.
    std::vector<std::size_t> driver(nl_.nets.size(), netlist::kNoNet);
    std::vector<std::vector<std::size_t>> readers(nl_.nets.size());
    std::vector<const tech::CellModel*> models(nl_.instances.size(), nullptr);

    for (std::size_t i = 0; i < nl_.instances.size(); ++i) {
        const netlist::Instance& inst = nl_.instances[i];
        const tech::CellModel* model = cells_.find(inst.cell);
        models[i] = model;
        if (model == nullptr) {
            warnings_.push_back(
                fmt::format("no behavioural model for cell '{}' ({})", inst.cell, inst.name));
            continue;
        }
        if (model->is_physical()) {
            continue;
        }
        for (const netlist::InstancePin& p : inst.pins) {
            if (p.net == netlist::kNoNet) {
                continue;
            }
            const tech::Pin* pin = model->find_pin(p.pin);
            if (pin == nullptr) {
                continue;
            }
            if (pin->direction == tech::PinDirection::Output) {
                if (driver[p.net] != netlist::kNoNet) {
                    warnings_.push_back(fmt::format("net '{}' is driven by more than one cell",
                                                    nl_.nets[p.net].name));
                }
                driver[p.net] = i;
            } else if (pin->direction == tech::PinDirection::Input) {
                readers[p.net].push_back(i);
            }
        }
    }

    // Flip-flops break the combinational graph.
    std::vector<bool> is_flop(nl_.instances.size(), false);
    for (std::size_t i = 0; i < nl_.instances.size(); ++i) {
        const tech::CellModel* model = models[i];
        if (model == nullptr || model->kind != tech::CellKind::Sequential) {
            continue;
        }
        is_flop[i] = true;
        Flop f;
        f.instance = i;
        f.model = model;
        const netlist::Instance& inst = nl_.instances[i];
        auto pin_net = [&](const std::string& name) -> std::size_t {
            const netlist::InstancePin* p = inst.find(name);
            return p == nullptr ? netlist::kNoNet : p->net;
        };
        f.clk = pin_net(model->seq.clock_pin);
        f.d = pin_net(model->seq.data_pin);
        f.q = pin_net(model->seq.q_pin);
        f.qn = model->seq.qn_pin.empty() ? netlist::kNoNet : pin_net(model->seq.qn_pin);
        f.reset = model->seq.reset_pin.empty() ? netlist::kNoNet : pin_net(model->seq.reset_pin);
        f.reset_active_low = model->seq.reset_active_low;
        f.set = model->seq.set_pin.empty() ? netlist::kNoNet : pin_net(model->seq.set_pin);
        f.set_active_low = model->seq.set_active_low;
        flops_.push_back(f);
    }

    // Topological order over the combinational instances only.
    std::vector<std::size_t> indegree(nl_.instances.size(), 0);
    std::vector<std::vector<std::size_t>> successors(nl_.instances.size());
    for (std::size_t i = 0; i < nl_.instances.size(); ++i) {
        const tech::CellModel* model = models[i];
        if (model == nullptr || model->kind != tech::CellKind::Combinational) {
            continue;
        }
        for (const netlist::InstancePin& p : nl_.instances[i].pins) {
            if (p.net == netlist::kNoNet) {
                continue;
            }
            const tech::Pin* pin = model->find_pin(p.pin);
            if (pin == nullptr || pin->direction != tech::PinDirection::Input) {
                continue;
            }
            const std::size_t src = driver[p.net];
            if (src == netlist::kNoNet || is_flop[src] || models[src] == nullptr ||
                models[src]->kind != tech::CellKind::Combinational) {
                continue;  // a primary input or a flip-flop output: already stable
            }
            successors[src].push_back(i);
            ++indegree[i];
        }
    }

    std::queue<std::size_t> ready;
    std::size_t combinational = 0;
    for (std::size_t i = 0; i < nl_.instances.size(); ++i) {
        const tech::CellModel* model = models[i];
        if (model == nullptr || model->kind != tech::CellKind::Combinational) {
            continue;
        }
        ++combinational;
        if (indegree[i] == 0) {
            ready.push(i);
        }
    }
    while (!ready.empty()) {
        const std::size_t i = ready.front();
        ready.pop();
        order_.push_back(Eval{i, models[i]});
        for (std::size_t s : successors[i]) {
            if (--indegree[s] == 0) {
                ready.push(s);
            }
        }
    }
    if (order_.size() != combinational) {
        warnings_.push_back(
            fmt::format("combinational loop detected: {} of {} gates could not be levelised",
                        combinational - order_.size(), combinational));
        for (std::size_t i = 0; i < nl_.instances.size(); ++i) {
            if (indegree[i] > 0 && models[i] != nullptr &&
                models[i]->kind == tech::CellKind::Combinational) {
                order_.push_back(Eval{i, models[i]});
            }
        }
    }

    for (std::size_t n = 0; n < nl_.nets.size(); ++n) {
        if (driver[n] == netlist::kNoNet && nl_.nets[n].kind == netlist::NetKind::Signal &&
            !readers[n].empty()) {
            free_inputs_.push_back(n);
        }
    }
}

void Simulator::reset_state() {
    std::fill(values_.begin(), values_.end(), Logic::Unknown);
    for (std::size_t i = 0; i < nl_.nets.size(); ++i) {
        if (nl_.nets[i].kind == netlist::NetKind::Power) {
            values_[i] = Logic::One;
        } else if (nl_.nets[i].kind == netlist::NetKind::Ground) {
            values_[i] = Logic::Zero;
        }
    }
    for (Flop& f : flops_) {
        f.state = Logic::Unknown;
    }
}

void Simulator::set_net(std::size_t net, Logic value) {
    if (net < values_.size()) {
        values_[net] = value;
    }
}

void Simulator::set_net(const std::string& name, Logic value) {
    const std::size_t idx = nl_.net_by_name(name);
    if (idx != netlist::kNoNet) {
        values_[idx] = value;
    }
}

Logic Simulator::get_net(std::size_t net) const {
    return net < values_.size() ? values_[net] : Logic::Unknown;
}

Logic Simulator::get_net(const std::string& name) const {
    const std::size_t idx = nl_.net_by_name(name);
    return idx == netlist::kNoNet ? Logic::Unknown : values_[idx];
}

Logic Simulator::eval_expr(const tech::Expr& e, const netlist::Instance& inst) const {
    switch (e.op) {
        case tech::Expr::Op::Const: return e.value ? Logic::One : Logic::Zero;

        case tech::Expr::Op::Var: {
            const netlist::InstancePin* p = inst.find(e.var);
            if (p == nullptr || p->net == netlist::kNoNet) {
                return Logic::Unknown;
            }
            return values_[p->net];
        }

        case tech::Expr::Op::Not: return logic_not(eval_expr(*e.args[0], inst));

        case tech::Expr::Op::And: {
            Logic acc = Logic::One;
            for (const auto& a : e.args) {
                acc = logic_and(acc, eval_expr(*a, inst));
            }
            return acc;
        }

        case tech::Expr::Op::Or: {
            Logic acc = Logic::Zero;
            for (const auto& a : e.args) {
                acc = logic_or(acc, eval_expr(*a, inst));
            }
            return acc;
        }

        case tech::Expr::Op::Xor: {
            Logic acc = Logic::Zero;
            for (const auto& a : e.args) {
                acc = logic_xor(acc, eval_expr(*a, inst));
            }
            return acc;
        }
    }
    return Logic::Unknown;
}

void Simulator::evaluate(const Eval& e) {
    const netlist::Instance& inst = nl_.instances[e.instance];
    for (const auto& [pin_name, fn] : e.model->functions) {
        const netlist::InstancePin* p = inst.find(pin_name);
        if (p == nullptr || p->net == netlist::kNoNet) {
            continue;
        }
        values_[p->net] = eval_expr(*fn, inst);
    }
}

void Simulator::apply_async(Flop& f) {
    if (f.reset != netlist::kNoNet) {
        const Logic r = values_[f.reset];
        const bool asserted = f.reset_active_low ? (r == Logic::Zero) : (r == Logic::One);
        if (asserted) {
            f.state = Logic::Zero;
        }
    }
    if (f.set != netlist::kNoNet) {
        const Logic s = values_[f.set];
        const bool asserted = f.set_active_low ? (s == Logic::Zero) : (s == Logic::One);
        if (asserted) {
            f.state = Logic::One;
        }
    }
    if (f.q != netlist::kNoNet) {
        values_[f.q] = f.state;
    }
    if (f.qn != netlist::kNoNet) {
        values_[f.qn] = logic_not(f.state);
    }
}

void Simulator::settle() {
    // Asynchronous resets are level sensitive, so they are re-applied on every
    // settle; then the levelised combinational cloud is evaluated once.
    for (Flop& f : flops_) {
        apply_async(f);
    }
    for (const Eval& e : order_) {
        evaluate(e);
    }
    // Reset can arrive through combinational logic, so give the flops a second
    // chance once the cloud has settled.
    bool changed = false;
    for (Flop& f : flops_) {
        const Logic before = f.state;
        apply_async(f);
        changed = changed || before != f.state;
    }
    if (changed) {
        for (const Eval& e : order_) {
            evaluate(e);
        }
    }
}

void Simulator::clock_edge(std::size_t clock_net) {
    if (clock_net == netlist::kNoNet) {
        return;
    }

    // Drive the clock low and let the clock tree settle, so each flop's local
    // clock net reflects the pre-edge level. Buffered and inverted branches are
    // handled for free because we look at the flop's own clock net, not at the
    // primary input.
    values_[clock_net] = Logic::Zero;
    settle();

    std::vector<Logic> sampled_d(flops_.size(), Logic::Unknown);
    std::vector<Logic> clock_before(flops_.size(), Logic::Unknown);
    for (std::size_t i = 0; i < flops_.size(); ++i) {
        const Flop& f = flops_[i];
        sampled_d[i] = f.d == netlist::kNoNet ? Logic::Unknown : values_[f.d];
        clock_before[i] = f.clk == netlist::kNoNet ? Logic::Unknown : values_[f.clk];
    }

    values_[clock_net] = Logic::One;
    settle();

    for (std::size_t i = 0; i < flops_.size(); ++i) {
        Flop& f = flops_[i];
        if (f.clk == netlist::kNoNet) {
            continue;
        }
        const Logic after = values_[f.clk];
        const bool rising = clock_before[i] == Logic::Zero && after == Logic::One;
        const bool falling = clock_before[i] == Logic::One && after == Logic::Zero;
        const bool triggered = f.model->seq.clock_posedge ? rising : falling;
        if (triggered) {
            f.state = sampled_d[i];
        }
    }
    settle();
}

void Simulator::clock_edge(const std::string& clock_name) {
    clock_edge(nl_.net_by_name(clock_name));
}

}  // namespace asicrev::sim
