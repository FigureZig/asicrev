#pragma once

#include <cstddef>
#include <numeric>
#include <vector>

namespace asicrev {

/// Disjoint-set forest with path halving and union by size.
class UnionFind {
public:
    UnionFind() = default;

    explicit UnionFind(std::size_t n) : parent_(n), size_(n, 1) {
        std::iota(parent_.begin(), parent_.end(), std::size_t{0});
    }

    std::size_t add() {
        parent_.push_back(parent_.size());
        size_.push_back(1);
        return parent_.size() - 1;
    }

    std::size_t size() const { return parent_.size(); }

    std::size_t find(std::size_t x) {
        while (parent_[x] != x) {
            parent_[x] = parent_[parent_[x]];
            x = parent_[x];
        }
        return x;
    }

    bool unite(std::size_t a, std::size_t b) {
        a = find(a);
        b = find(b);
        if (a == b) {
            return false;
        }
        if (size_[a] < size_[b]) {
            std::swap(a, b);
        }
        parent_[b] = a;
        size_[a] += size_[b];
        return true;
    }

    bool connected(std::size_t a, std::size_t b) { return find(a) == find(b); }

private:
    std::vector<std::size_t> parent_;
    std::vector<std::size_t> size_;
};

}  // namespace asicrev
