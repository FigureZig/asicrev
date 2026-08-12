#include "asicrev/def/def_reader.hpp"

#include <fmt/format.h>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace asicrev::def {

namespace {

/// DEF is whitespace separated with `;` terminators, so a plain token stream
/// is enough for the sections we care about.
class Tokens {
public:
    explicit Tokens(std::istream& in) : in_(in) {}

    bool next(std::string& out) { return static_cast<bool>(in_ >> out); }

    std::string expect() {
        std::string t;
        if (!next(t)) {
            throw std::runtime_error("unexpected end of DEF file");
        }
        return t;
    }

    /// Skip to the token after the next `;`.
    void skip_statement() {
        std::string t;
        while (next(t)) {
            if (t == ";") {
                return;
            }
        }
    }

    /// Consume the section name that follows an `END` token. Section
    /// terminators are not `;`-terminated, so skip_statement() would run on
    /// into the next section header and swallow it.
    void finish_section() {
        std::string t;
        next(t);
    }

private:
    std::istream& in_;
};

Dbu to_dbu(const std::string& s) {
    return static_cast<Dbu>(std::stoll(s));
}

}  // namespace

Design read_def(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error(fmt::format("cannot open '{}'", path.string()));
    }
    Design design;
    Tokens tok(in);
    std::string t;

    while (tok.next(t)) {
        if (t == "END") {
            // `END DESIGN` closes the file; consume the name so that DESIGN is
            // not mistaken for a second design header.
            tok.finish_section();
        } else if (t == "DESIGN") {
            design.name = tok.expect();
            tok.skip_statement();
        } else if (t == "UNITS") {
            // UNITS DISTANCE MICRONS <n> ;
            tok.expect();  // DISTANCE
            tok.expect();  // MICRONS
            design.units_per_micron = to_dbu(tok.expect());
            tok.skip_statement();
        } else if (t == "DIEAREA") {
            std::vector<Dbu> coords;
            std::string s;
            while (tok.next(s) && s != ";") {
                if (s != "(" && s != ")") {
                    coords.push_back(to_dbu(s));
                }
            }
            if (coords.size() >= 4) {
                design.die_area = Rect::from_points(coords[0], coords[1], coords[2], coords[3]);
            }
        } else if (t == "COMPONENTS") {
            tok.skip_statement();
            while (tok.next(t) && t != "END") {
                if (t != "-") {
                    continue;
                }
                Component c;
                c.name = tok.expect();
                c.cell = tok.expect();
                std::string s;
                while (tok.next(s) && s != ";") {
                    if (s == "PLACED" || s == "FIXED" || s == "COVER") {
                        tok.expect();  // (
                        c.position.x = to_dbu(tok.expect());
                        c.position.y = to_dbu(tok.expect());
                        tok.expect();  // )
                        c.orientation = tok.expect();
                    }
                }
                design.components.push_back(std::move(c));
            }
            tok.finish_section();
        } else if (t == "PINS") {
            tok.skip_statement();
            while (tok.next(t) && t != "END") {
                if (t != "-") {
                    continue;
                }
                Pin p;
                p.name = tok.expect();
                std::string s;
                while (tok.next(s) && s != ";") {
                    if (s == "+") {
                        continue;
                    }
                    if (s == "NET") {
                        p.net = tok.expect();
                    } else if (s == "DIRECTION") {
                        p.direction = tok.expect();
                    }
                }
                design.pins.push_back(std::move(p));
            }
            tok.finish_section();
        } else if (t == "NETS" || t == "SPECIALNETS") {
            const bool special = t == "SPECIALNETS";
            tok.skip_statement();
            while (tok.next(t) && t != "END") {
                if (t != "-") {
                    continue;
                }
                Net n;
                n.special = special;
                n.name = tok.expect();
                // Connections come as `( instance pin )` groups before the
                // first `+` attribute; routing after that is not our business.
                std::string s;
                bool in_attributes = false;
                while (tok.next(s) && s != ";") {
                    if (s == "+") {
                        in_attributes = true;
                        continue;
                    }
                    if (in_attributes || s != "(") {
                        continue;
                    }
                    NetPin np;
                    const std::string a = tok.expect();
                    const std::string b = tok.expect();
                    if (a == "PIN") {
                        np.pin = b;
                    } else {
                        np.instance = a;
                        np.pin = b;
                    }
                    std::string close = tok.expect();
                    while (close != ")") {
                        close = tok.expect();
                    }
                    n.pins.push_back(std::move(np));
                }
                design.nets.push_back(std::move(n));
            }
            tok.finish_section();
        }
    }

    return design;
}

}  // namespace asicrev::def
