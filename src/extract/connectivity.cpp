#include "asicrev/extract/connectivity.hpp"

#include <algorithm>
#include <stdexcept>

namespace asicrev::extract {

// --------------------------------------------------------------- GridIndex

void GridIndex::build(const std::vector<LayerRect>& all, const std::vector<std::size_t>& handles,
                      Dbu cell_size) {
    starts_.clear();
    items_.clear();
    if (handles.empty()) {
        return;
    }
    cell_size_ = std::max<Dbu>(cell_size, 1);

    Rect bb = all[handles.front()].rect;
    for (std::size_t h : handles) {
        bb.expand(all[h].rect);
    }
    ox_ = bb.xlo;
    oy_ = bb.ylo;
    nx_ = (bb.xhi - bb.xlo) / cell_size_ + 1;
    ny_ = (bb.yhi - bb.ylo) / cell_size_ + 1;

    const auto bucket_count = static_cast<std::size_t>(nx_ * ny_);
    std::vector<std::size_t> counts(bucket_count, 0);

    auto visit = [&](const Rect& r, auto&& fn) {
        const auto [x0, y0] = cell_of(r.xlo, r.ylo);
        const auto [x1, y1] = cell_of(r.xhi, r.yhi);
        for (std::int64_t gy = y0; gy <= y1; ++gy) {
            for (std::int64_t gx = x0; gx <= x1; ++gx) {
                fn(static_cast<std::size_t>(gy * nx_ + gx));
            }
        }
    };

    for (std::size_t h : handles) {
        visit(all[h].rect, [&](std::size_t b) { ++counts[b]; });
    }

    starts_.assign(bucket_count + 1, 0);
    for (std::size_t i = 0; i < bucket_count; ++i) {
        starts_[i + 1] = starts_[i] + counts[i];
    }
    items_.resize(starts_.back());

    std::vector<std::size_t> cursor(starts_.begin(), starts_.end() - 1);
    for (std::size_t h : handles) {
        visit(all[h].rect, [&](std::size_t b) { items_[cursor[b]++] = h; });
    }
}

std::pair<std::int64_t, std::int64_t> GridIndex::cell_of(Dbu x, Dbu y) const {
    std::int64_t gx = (x - ox_) / cell_size_;
    std::int64_t gy = (y - oy_) / cell_size_;
    if (x < ox_) {
        gx = 0;
    }
    if (y < oy_) {
        gy = 0;
    }
    gx = std::clamp<std::int64_t>(gx, 0, nx_ - 1);
    gy = std::clamp<std::int64_t>(gy, 0, ny_ - 1);
    return {gx, gy};
}

// ------------------------------------------------------- ConnectivityBuilder

ConnectivityBuilder::ConnectivityBuilder(std::size_t conductor_count, Dbu grid_size)
    : conductor_count_(conductor_count),
      grid_size_(grid_size),
      by_layer_(conductor_count),
      index_(conductor_count) {}

std::size_t ConnectivityBuilder::add_conductor(const Rect& r, std::size_t conductor,
                                               std::size_t owner) {
    if (conductor >= conductor_count_) {
        throw std::out_of_range("conductor index out of range");
    }
    const std::size_t handle = rects_.size();
    rects_.push_back(LayerRect{r, conductor, owner});
    by_layer_[conductor].push_back(handle);
    uf_.add();
    built_ = false;
    return handle;
}

void ConnectivityBuilder::add_cut(const Rect& r, std::size_t lower) {
    if (lower + 1 >= conductor_count_) {
        return;  // a cut into nothing: the stack ends here
    }
    cuts_.push_back(CutRect{r, lower});
    built_ = false;
}

void ConnectivityBuilder::build() {
    for (std::size_t c = 0; c < conductor_count_; ++c) {
        index_[c].build(rects_, by_layer_[c], grid_size_);
    }
    merge_same_layer();
    stitch_cuts();
    built_ = true;
}

void ConnectivityBuilder::merge_same_layer() {
    for (std::size_t c = 0; c < conductor_count_; ++c) {
        for (std::size_t h : by_layer_[c]) {
            const Rect& r = rects_[h].rect;
            index_[c].for_each_candidate(r, [&](std::size_t other) {
                if (other <= h) {
                    return;  // each unordered pair is visited once
                }
                if (r.touches(rects_[other].rect)) {
                    uf_.unite(h, other);
                }
            });
        }
    }
}

void ConnectivityBuilder::stitch_cuts() {
    for (const CutRect& cut : cuts_) {
        std::size_t anchor = static_cast<std::size_t>(-1);
        for (std::size_t level : {cut.lower, cut.lower + 1}) {
            index_[level].for_each_candidate(cut.rect, [&](std::size_t h) {
                if (!rects_[h].rect.overlaps(cut.rect)) {
                    return;
                }
                if (anchor == static_cast<std::size_t>(-1)) {
                    anchor = h;
                } else {
                    uf_.unite(anchor, h);
                }
            });
        }
    }
}

std::size_t ConnectivityBuilder::net_of_rect(std::size_t handle) {
    return uf_.find(handle);
}

std::optional<std::size_t> ConnectivityBuilder::net_at(Point p, std::size_t conductor) {
    if (conductor >= conductor_count_) {
        return std::nullopt;
    }
    const Rect probe{p.x, p.y, p.x, p.y};
    std::optional<std::size_t> found;
    index_[conductor].for_each_candidate(probe, [&](std::size_t h) {
        if (!found.has_value() && rects_[h].rect.contains(p)) {
            found = uf_.find(h);
        }
    });
    return found;
}

std::size_t ConnectivityBuilder::net_count() {
    std::vector<std::size_t> roots;
    roots.reserve(rects_.size());
    for (std::size_t i = 0; i < rects_.size(); ++i) {
        roots.push_back(uf_.find(i));
    }
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    return roots.size();
}

}  // namespace asicrev::extract
