#pragma once

#include "asicrev/common/geometry.hpp"
#include "asicrev/common/union_find.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace asicrev::extract {

/// A rectangle living on one conductor level of the stack.
struct LayerRect {
    Rect rect;
    std::size_t conductor = 0;
    std::size_t owner = 0;  ///< index of the polygon it was decomposed from
};

/// Uniform-grid spatial index over a subset of rectangles.
///
/// Layouts are dense and roughly uniform, so a bucketed grid beats a full
/// R-tree here in both build time and query time, and needs no rebalancing.
class GridIndex {
public:
    GridIndex() = default;

    void build(const std::vector<LayerRect>& all, const std::vector<std::size_t>& handles,
               Dbu cell_size);

    /// Call `fn(handle)` for every rectangle sharing a bucket with `query`.
    /// May report the same handle more than once; callers must tolerate that.
    template<typename Fn>
    void for_each_candidate(const Rect& query, Fn&& fn) const {
        if (items_.empty()) {
            return;
        }
        const auto [x0, y0] = cell_of(query.xlo, query.ylo);
        const auto [x1, y1] = cell_of(query.xhi, query.yhi);
        for (std::int64_t gy = y0; gy <= y1; ++gy) {
            for (std::int64_t gx = x0; gx <= x1; ++gx) {
                const std::size_t b = static_cast<std::size_t>(gy * nx_ + gx);
                for (std::size_t i = starts_[b]; i < starts_[b + 1]; ++i) {
                    fn(items_[i]);
                }
            }
        }
    }

    bool empty() const { return items_.empty(); }

private:
    std::pair<std::int64_t, std::int64_t> cell_of(Dbu x, Dbu y) const;

    Dbu cell_size_ = 1;
    std::int64_t nx_ = 1;
    std::int64_t ny_ = 1;
    Dbu ox_ = 0;
    Dbu oy_ = 0;
    std::vector<std::size_t> starts_;
    std::vector<std::size_t> items_;
};

/// Merge same-layer rectangles that touch, then stitch layers through cuts.
class ConnectivityBuilder {
public:
    ConnectivityBuilder(std::size_t conductor_count, Dbu grid_size);

    /// Register a conductor rectangle; returns its handle.
    std::size_t add_conductor(const Rect& r, std::size_t conductor, std::size_t owner);

    /// Register a via/contact cut spanning conductors `lower` and `lower + 1`.
    void add_cut(const Rect& r, std::size_t lower);

    /// Run the merge. Must be called before any query.
    void build();

    /// Net id (a union-find root) of the conductor rectangle `handle`.
    std::size_t net_of_rect(std::size_t handle);

    /// Net id of the shape covering `p` on conductor level `conductor`.
    std::optional<std::size_t> net_at(Point p, std::size_t conductor);

    std::size_t rect_count() const { return rects_.size(); }

    /// Number of distinct nets after build().
    std::size_t net_count();

    const std::vector<LayerRect>& rects() const { return rects_; }

private:
    struct CutRect {
        Rect rect;
        std::size_t lower;
    };

    void merge_same_layer();
    void stitch_cuts();

    std::size_t conductor_count_;
    Dbu grid_size_;
    std::vector<LayerRect> rects_;
    std::vector<CutRect> cuts_;
    std::vector<std::vector<std::size_t>> by_layer_;  ///< rect handles per conductor
    std::vector<GridIndex> index_;
    UnionFind uf_;
    bool built_ = false;
};

}  // namespace asicrev::extract
