#include "asicrev/netlist/verilog_reader.hpp"

#include <fmt/format.h>

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace asicrev::netlist {

namespace {

/// Token stream for the structural subset of Verilog. Identifiers may be
/// escaped (`\a_reg[0] `), which is how synthesis writes bus bits and
/// hierarchical names into a flat netlist.
class Lexer {
public:
    Lexer(const std::string& text, std::string origin) : text_(text), origin_(std::move(origin)) {}

    std::string next() {
        skip_trivia();
        if (pos_ >= text_.size()) {
            return {};
        }
        const char c = text_[pos_];
        if (c == '\\') {
            const std::size_t start = ++pos_;
            while (pos_ < text_.size() &&
                   (std::isspace(static_cast<unsigned char>(text_[pos_])) == 0)) {
                ++pos_;
            }
            return text_.substr(start, pos_ - start);
        }
        if ((std::isalpha(static_cast<unsigned char>(c)) != 0) || c == '_' || c == '$') {
            const std::size_t start = pos_;
            while (pos_ < text_.size() &&
                   ((std::isalnum(static_cast<unsigned char>(text_[pos_])) != 0) ||
                    text_[pos_] == '_' || text_[pos_] == '$')) {
                ++pos_;
            }
            return text_.substr(start, pos_ - start);
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            const std::size_t start = pos_;
            while (pos_ < text_.size() &&
                   ((std::isalnum(static_cast<unsigned char>(text_[pos_])) != 0) ||
                    text_[pos_] == '\'' || text_[pos_] == '_')) {
                ++pos_;
            }
            return text_.substr(start, pos_ - start);
        }
        ++pos_;
        return std::string(1, c);
    }

    std::string peek() {
        const std::size_t save = pos_;
        std::string t = next();
        pos_ = save;
        return t;
    }

    [[noreturn]] void fail(const std::string& message) const {
        std::size_t line = 1;
        for (std::size_t i = 0; i < pos_ && i < text_.size(); ++i) {
            line += text_[i] == char{0x0a} ? std::size_t{1} : std::size_t{0};
        }
        throw std::runtime_error(fmt::format("{}:{}: {}", origin_, line, message));
    }

private:
    void skip_trivia() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                ++pos_;
            } else if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
                while (pos_ < text_.size() && text_[pos_] != '\n') {
                    ++pos_;
                }
            } else if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '*') {
                pos_ += 2;
                while (pos_ + 1 < text_.size() && !(text_[pos_] == '*' && text_[pos_ + 1] == '/')) {
                    ++pos_;
                }
                pos_ = std::min(pos_ + 2, text_.size());
            } else if (c == '(' && text_.compare(pos_, 2, "(*") == 0) {
                const std::size_t end = text_.find("*)", pos_);
                pos_ = end == std::string::npos ? text_.size() : end + 2;
            } else {
                return;
            }
        }
    }

    const std::string& text_;
    std::string origin_;
    std::size_t pos_ = 0;
};

bool is_punct(const std::string& t) {
    return t.size() == 1 && (std::isalnum(static_cast<unsigned char>(t[0])) == 0) && t[0] != '_';
}

}  // namespace

Netlist parse_structural_verilog(const std::string& text, const std::string& origin) {
    Lexer lex(text, origin);
    Netlist nl;
    std::unordered_map<std::string, std::size_t> net_index;

    auto net_for = [&](const std::string& name) -> std::size_t {
        const auto it = net_index.find(name);
        if (it != net_index.end()) {
            return it->second;
        }
        NetKind kind = NetKind::Signal;
        if (name == "VPWR" || name == "VPB" || name == "VDD") {
            kind = NetKind::Power;
        } else if (name == "VGND" || name == "VNB" || name == "VSS") {
            kind = NetKind::Ground;
        }
        const std::size_t idx = nl.add_net(name, kind);
        net_index.emplace(name, idx);
        return idx;
    };

    // Skip everything before the first module header.
    std::string tok;
    while (!(tok = lex.next()).empty() && tok != "module") {}
    if (tok != "module") {
        throw std::runtime_error(fmt::format("{}: no module found", origin));
    }
    nl.module_name = lex.next();

    // Port list: names only, direction comes from the declarations below.
    std::vector<std::string> port_order;
    if (lex.peek() == "(") {
        lex.next();
        int depth = 1;
        while (depth > 0) {
            std::string t = lex.next();
            if (t.empty()) {
                lex.fail("unterminated module port list");
            }
            if (t == "(") {
                ++depth;
            } else if (t == ")") {
                --depth;
            } else if (t != "," && !is_punct(t)) {
                port_order.push_back(t);
            }
        }
    }
    if (lex.peek() == ";") {
        lex.next();
    }

    std::unordered_map<std::string, PortDirection> directions;

    while (true) {
        std::string t = lex.next();
        if (t.empty() || t == "endmodule") {
            break;
        }

        if (t == "input" || t == "output" || t == "inout" || t == "wire" || t == "reg") {
            const bool is_port = t != "wire" && t != "reg";
            PortDirection dir = PortDirection::Input;
            if (t == "output") {
                dir = PortDirection::Output;
            } else if (t == "inout") {
                dir = PortDirection::InOut;
            }
            // Optional `wire` keyword and range, then a comma separated list.
            std::string n = lex.next();
            if (n == "wire" || n == "reg" || n == "signed") {
                n = lex.next();
            }
            if (n == "[") {
                while (!n.empty() && n != "]") {
                    n = lex.next();
                }
                n = lex.next();
            }
            while (!n.empty() && n != ";") {
                if (n != ",") {
                    net_for(n);
                    if (is_port) {
                        directions[n] = dir;
                        if (std::find(port_order.begin(), port_order.end(), n) ==
                            port_order.end()) {
                            port_order.push_back(n);
                        }
                    }
                }
                n = lex.next();
            }
            continue;
        }

        if (t == "assign") {
            // `assign lhs = rhs;` - only the trivial alias form is meaningful
            // in a structural netlist, and it simply merges two net names.
            const std::string lhs = lex.next();
            std::string eq = lex.next();
            std::vector<std::string> rhs;
            std::string n = lex.next();
            while (!n.empty() && n != ";") {
                rhs.push_back(n);
                n = lex.next();
            }
            if (eq == "=" && rhs.size() == 1) {
                const std::size_t a = net_for(lhs);
                const std::size_t b = net_for(rhs.front());
                if (a != b) {
                    // Represent the alias with a zero-delay buffer instance so
                    // the graph stays a pure cell/net bipartite graph.
                    Instance inst;
                    inst.name = fmt::format("$alias${}", nl.instances.size());
                    inst.cell = "$alias";
                    inst.pins.push_back(InstancePin{"A", b});
                    inst.pins.push_back(InstancePin{"X", a});
                    nl.instances.push_back(std::move(inst));
                }
            }
            continue;
        }

        if (is_punct(t)) {
            continue;  // stray semicolons and the like
        }

        // Otherwise: `<cell> <instance> ( .PIN(net), ... );`
        const std::string cell = t;
        std::string inst_name = lex.next();
        if (inst_name == "(") {
            lex.fail(fmt::format("instantiation of '{}' without an instance name", cell));
        }
        if (lex.peek() != "(") {
            continue;  // not something we understand; skip to the next statement
        }
        lex.next();

        Instance inst;
        inst.name = inst_name;
        inst.cell = cell;
        while (true) {
            std::string p = lex.next();
            if (p.empty()) {
                lex.fail(fmt::format("unterminated port list on instance '{}'", inst_name));
            }
            if (p == ")") {
                break;
            }
            if (p == ",") {
                continue;
            }
            if (p != ".") {
                lex.fail(fmt::format("instance '{}' uses positional ports, which are not supported",
                                     inst_name));
            }
            const std::string pin = lex.next();
            if (lex.next() != "(") {
                lex.fail(fmt::format("malformed connection to {}.{}", inst_name, pin));
            }
            std::vector<std::string> parts;
            std::string q = lex.next();
            int depth = 0;
            while (!q.empty() && !(q == ")" && depth == 0)) {
                if (q == "(" || q == "{") {
                    ++depth;
                } else if (q == ")" || q == "}") {
                    --depth;
                } else {
                    parts.push_back(q);
                }
                q = lex.next();
            }
            if (parts.size() == 1) {
                inst.pins.push_back(InstancePin{pin, net_for(parts.front())});
            } else if (parts.empty()) {
                inst.pins.push_back(InstancePin{pin, kNoNet});  // unconnected
            } else {
                // Constant or expression: model it as a dedicated net so the
                // comparison still sees a distinct connection.
                inst.pins.push_back(InstancePin{pin, net_for(fmt::format("${}", parts.front()))});
            }
        }
        if (lex.peek() == ";") {
            lex.next();
        }
        std::sort(inst.pins.begin(), inst.pins.end(),
                  [](const auto& a, const auto& b) { return a.pin < b.pin; });
        nl.instances.push_back(std::move(inst));
    }

    for (const std::string& name : port_order) {
        const auto dir = directions.find(name);
        Port p;
        p.name = name;
        p.direction = dir == directions.end() ? PortDirection::Input : dir->second;
        p.net = net_for(name);
        nl.ports.push_back(std::move(p));
    }
    std::sort(nl.ports.begin(), nl.ports.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });

    return nl;
}

Netlist read_structural_verilog(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error(fmt::format("cannot open '{}'", path.string()));
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parse_structural_verilog(buffer.str(), path.string());
}

}  // namespace asicrev::netlist
