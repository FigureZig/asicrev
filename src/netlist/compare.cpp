#include "asicrev/netlist/compare.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <functional>
#include <map>
#include <numeric>
#include <set>

namespace asicrev::netlist {

namespace {

/// Bipartite instance/net graph with the pin names kept on the edges.
struct Graph {
    struct Node {
        bool is_instance = false;
        std::string cell;  ///< instances only
        std::string name;
        bool pinned = false;                                     ///< nets named in both designs
        std::vector<std::pair<std::string, std::size_t>> edges;  ///< (pin, neighbour)
    };

    std::vector<Node> nodes;
    std::map<std::string, std::size_t> net_node;  ///< net name -> node index
};

Graph build_graph(const Netlist& nl, const tech::CellLibrary& cells,
                  const CompareOptions& options) {
    Graph g;
    std::vector<std::size_t> net_to_node(nl.nets.size(), static_cast<std::size_t>(-1));

    for (std::size_t i = 0; i < nl.nets.size(); ++i) {
        if (options.ignore_power_nets && nl.nets[i].kind != NetKind::Signal) {
            continue;
        }
        Graph::Node node;
        node.is_instance = false;
        node.name = nl.nets[i].name;
        net_to_node[i] = g.nodes.size();
        g.net_node.emplace(node.name, g.nodes.size());
        g.nodes.push_back(std::move(node));
    }

    for (const Instance& inst : nl.instances) {
        const tech::CellModel* model = cells.find(inst.cell);
        if (options.ignore_physical_cells && model != nullptr && model->is_physical()) {
            continue;
        }
        Graph::Node node;
        node.is_instance = true;
        node.cell = inst.cell;
        node.name = inst.name;
        const std::size_t idx = g.nodes.size();
        g.nodes.push_back(std::move(node));

        for (const InstancePin& p : inst.pins) {
            if (p.net == kNoNet || net_to_node[p.net] == static_cast<std::size_t>(-1)) {
                continue;
            }
            const std::size_t net_node = net_to_node[p.net];
            g.nodes[idx].edges.emplace_back(p.pin, net_node);
            g.nodes[net_node].edges.emplace_back(p.pin, idx);
        }
    }

    for (Graph::Node& n : g.nodes) {
        std::sort(n.edges.begin(), n.edges.end());
    }
    return g;
}

std::string seed_key(const Graph& g, std::size_t i) {
    if (g.nodes[i].is_instance) {
        return "C:" + g.nodes[i].cell;
    }
    return g.nodes[i].pinned ? "P:" + g.nodes[i].name : "N";
}

/// Assign each distinct key an id equal to its rank in the sorted key set.
///
/// Ranking (rather than insertion order) is what makes the two colourings
/// comparable: both graphs must agree on which id a given signature gets, no
/// matter what order their nodes happen to be stored in.
template<typename Key>
std::map<Key, std::uint64_t> rank_keys(const std::vector<Key>& left,
                                       const std::vector<Key>& right) {
    std::map<Key, std::uint64_t> ranks;
    for (const Key& k : left) {
        ranks.emplace(k, 0);
    }
    for (const Key& k : right) {
        ranks.emplace(k, 0);
    }
    std::uint64_t next = 0;
    for (auto& [key, id] : ranks) {
        id = next++;
    }
    return ranks;
}

/// Weisfeiler-Lehman colour refinement run on both graphs at once.
///
/// Refining them jointly, with one shared signature-to-id table per round,
/// keeps the two colourings in lockstep; refining separately would hand the
/// same structural signature different ids in each graph.
std::size_t refine_pair(const Graph& gl, const Graph& gr, std::vector<std::uint64_t>& cl,
                        std::vector<std::uint64_t>& cr) {
    const auto signature = [](const Graph& g, const std::vector<std::uint64_t>& colour,
                              std::size_t i) {
        std::vector<std::uint64_t> sig;
        sig.reserve(g.nodes[i].edges.size() + 1);
        sig.push_back(colour[i]);
        std::vector<std::uint64_t> neighbours;
        neighbours.reserve(g.nodes[i].edges.size());
        for (const auto& [pin, nb] : g.nodes[i].edges) {
            // Fold the pin name into the neighbour's colour so A and B of an
            // asymmetric gate stay distinguishable.
            std::uint64_t h = std::hash<std::string>{}(pin);
            h ^= colour[nb] + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            neighbours.push_back(h);
        }
        std::sort(neighbours.begin(), neighbours.end());
        sig.insert(sig.end(), neighbours.begin(), neighbours.end());
        return sig;
    };

    const std::size_t limit = gl.nodes.size() + gr.nodes.size() + 2;
    std::size_t rounds = 0;
    while (rounds < limit) {
        std::vector<std::vector<std::uint64_t>> sl(gl.nodes.size());
        std::vector<std::vector<std::uint64_t>> sr(gr.nodes.size());
        for (std::size_t i = 0; i < gl.nodes.size(); ++i) {
            sl[i] = signature(gl, cl, i);
        }
        for (std::size_t i = 0; i < gr.nodes.size(); ++i) {
            sr[i] = signature(gr, cr, i);
        }

        const auto ranks = rank_keys(sl, sr);
        std::vector<std::uint64_t> next_l(gl.nodes.size());
        std::vector<std::uint64_t> next_r(gr.nodes.size());
        for (std::size_t i = 0; i < gl.nodes.size(); ++i) {
            next_l[i] = ranks.at(sl[i]);
        }
        for (std::size_t i = 0; i < gr.nodes.size(); ++i) {
            next_r[i] = ranks.at(sr[i]);
        }

        ++rounds;
        const bool changed = next_l != cl || next_r != cr;
        cl = std::move(next_l);
        cr = std::move(next_r);
        if (!changed) {
            break;
        }
    }
    return rounds;
}

void seed_colours(const Graph& gl, const Graph& gr, std::vector<std::uint64_t>& cl,
                  std::vector<std::uint64_t>& cr) {
    std::vector<std::string> kl(gl.nodes.size());
    std::vector<std::string> kr(gr.nodes.size());
    for (std::size_t i = 0; i < gl.nodes.size(); ++i) {
        kl[i] = seed_key(gl, i);
    }
    for (std::size_t i = 0; i < gr.nodes.size(); ++i) {
        kr[i] = seed_key(gr, i);
    }
    const auto ranks = rank_keys(kl, kr);
    cl.resize(gl.nodes.size());
    cr.resize(gr.nodes.size());
    for (std::size_t i = 0; i < gl.nodes.size(); ++i) {
        cl[i] = ranks.at(kl[i]);
    }
    for (std::size_t i = 0; i < gr.nodes.size(); ++i) {
        cr[i] = ranks.at(kr[i]);
    }
}

}  // namespace

CompareResult compare_netlists(const Netlist& left, const Netlist& right,
                               const tech::CellLibrary& cells, const CompareOptions& options) {
    CompareResult result;

    Graph gl = build_graph(left, cells, options);
    Graph gr = build_graph(right, cells, options);

    // Pin nets that carry the same name on both sides: in practice these are
    // the design's ports, which anchors the search immediately.
    for (auto& [name, idx] : gl.net_node) {
        if (gr.net_node.contains(name)) {
            gl.nodes[idx].pinned = true;
            gr.nodes[gr.net_node.at(name)].pinned = true;
        }
    }

    if (gl.nodes.size() != gr.nodes.size()) {
        result.differences.push_back(
            fmt::format("node count differs: {} vs {}", gl.nodes.size(), gr.nodes.size()));
    }

    const auto count_kind = [](const Graph& g, bool instances) {
        return std::count_if(g.nodes.begin(), g.nodes.end(),
                             [&](const Graph::Node& n) { return n.is_instance == instances; });
    };
    if (count_kind(gl, true) != count_kind(gr, true)) {
        result.differences.push_back(fmt::format("instance count differs: {} vs {}",
                                                 count_kind(gl, true), count_kind(gr, true)));
    }
    if (count_kind(gl, false) != count_kind(gr, false)) {
        result.differences.push_back(fmt::format("net count differs: {} vs {}",
                                                 count_kind(gl, false), count_kind(gr, false)));
    }

    // Cell histograms must match before an isomorphism can exist.
    std::map<std::string, int> hist;
    for (const Graph::Node& n : gl.nodes) {
        if (n.is_instance) {
            ++hist[n.cell];
        }
    }
    for (const Graph::Node& n : gr.nodes) {
        if (n.is_instance) {
            --hist[n.cell];
        }
    }
    for (const auto& [cell, delta] : hist) {
        if (delta != 0) {
            result.differences.push_back(
                fmt::format("cell '{}' appears {} extra time(s) on the left", cell, delta));
        }
    }
    if (!result.differences.empty()) {
        return result;
    }

    std::vector<std::uint64_t> cl;
    std::vector<std::uint64_t> cr;
    seed_colours(gl, gr, cl, cr);
    result.refinement_rounds = refine_pair(gl, gr, cl, cr);

    // Group nodes by colour and check the class sizes agree.
    std::map<std::uint64_t, std::vector<std::size_t>> left_classes;
    std::map<std::uint64_t, std::vector<std::size_t>> right_classes;
    for (std::size_t i = 0; i < cl.size(); ++i) {
        left_classes[cl[i]].push_back(i);
    }
    for (std::size_t i = 0; i < cr.size(); ++i) {
        right_classes[cr[i]].push_back(i);
    }
    if (left_classes.size() != right_classes.size()) {
        result.differences.push_back(
            fmt::format("colour refinement produced {} classes on the left and {} on the right",
                        left_classes.size(), right_classes.size()));
        return result;
    }
    for (const auto& [colour, members] : left_classes) {
        const auto it = right_classes.find(colour);
        if (it == right_classes.end() || it->second.size() != members.size()) {
            result.differences.push_back(fmt::format(
                "colour class {} has {} members on the left and {} on the right", colour,
                members.size(), it == right_classes.end() ? 0 : it->second.size()));
            return result;
        }
    }

    // Backtracking search over the refined classes. With a stable colouring the
    // classes are singletons for real netlists, so this is effectively a check.
    std::vector<std::size_t> map_lr(gl.nodes.size(), static_cast<std::size_t>(-1));
    std::vector<bool> used(gr.nodes.size(), false);

    std::vector<std::size_t> order(gl.nodes.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return left_classes.at(cl[a]).size() < left_classes.at(cl[b]).size();
    });

    std::size_t backtracks = 0;
    const std::function<bool(std::size_t)> assign = [&](std::size_t k) -> bool {
        if (k == order.size()) {
            return true;
        }
        const std::size_t u = order[k];
        if (map_lr[u] != static_cast<std::size_t>(-1)) {
            return assign(k + 1);
        }
        for (std::size_t vtx : right_classes.at(cl[u])) {
            if (used[vtx]) {
                continue;
            }
            const Graph::Node& a = gl.nodes[u];
            const Graph::Node& b = gr.nodes[vtx];
            if (a.is_instance != b.is_instance || a.cell != b.cell ||
                a.edges.size() != b.edges.size()) {
                continue;
            }

            // The two edge lists are sorted by (pin, neighbour index), and the
            // indices are unrelated across graphs, so they cannot be compared
            // positionally. Compare the pin-name multisets, then require every
            // already-mapped neighbour of `u` to appear on `vtx` through the
            // same pin.
            bool ok = true;
            for (std::size_t e = 0; e < a.edges.size(); ++e) {
                if (a.edges[e].first != b.edges[e].first) {
                    ok = false;  // sorted, so equal multisets compare elementwise
                    break;
                }
            }
            if (!ok) {
                continue;
            }

            std::multiset<std::pair<std::string, std::size_t>> available(b.edges.begin(),
                                                                         b.edges.end());
            for (const auto& [pin, neighbour] : a.edges) {
                if (map_lr[neighbour] == static_cast<std::size_t>(-1)) {
                    continue;
                }
                const auto it = available.find({pin, map_lr[neighbour]});
                if (it == available.end()) {
                    ok = false;
                    break;
                }
                available.erase(it);
            }
            if (!ok) {
                continue;
            }
            map_lr[u] = vtx;
            used[vtx] = true;
            if (assign(k + 1)) {
                return true;
            }
            map_lr[u] = static_cast<std::size_t>(-1);
            used[vtx] = false;
            ++backtracks;
        }
        return false;
    };

    if (!assign(0)) {
        result.backtracks = backtracks;
        result.differences.push_back(
            "no consistent one-to-one mapping exists between the two netlists");
        return result;
    }
    result.backtracks = backtracks;

    for (std::size_t i = 0; i < gl.nodes.size(); ++i) {
        const std::size_t j = map_lr[i];
        if (gl.nodes[i].is_instance) {
            result.instance_map.emplace(gl.nodes[i].name, gr.nodes[j].name);
        } else {
            result.net_map.emplace(gl.nodes[i].name, gr.nodes[j].name);
        }
    }
    result.equivalent = true;
    return result;
}

}  // namespace asicrev::netlist
