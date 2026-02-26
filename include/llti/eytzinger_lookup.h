#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace llti {

// Eytzinger (BFS) layout for cache-oblivious binary search.
//
// Stores keys in breadth-first order of an implicit binary tree.
// Node at 1-indexed position i has children at 2i and 2i+1.
// The first few tree levels pack into the same cache line, so
// the top of the tree is always hot in L1/L2.
//
// The search loop is branchless: i = 2*i + (keys[i] < target).
// Software prefetch fetches the next tree level each iteration.

template <typename Value>
struct EytzingerLookup {
    // 1-indexed: keys[0] is unused padding, tree root is keys[1]
    std::vector<int64_t> keys;
    std::vector<Value> vals;
    size_t n = 0;  // number of actual elements

    void build(std::vector<std::pair<int64_t, Value>> entries) {
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        n = entries.size();
        if (n == 0) return;

        keys.resize(n + 1);
        vals.resize(n + 1);

        // Recursively fill BFS positions from sorted order
        size_t sorted_idx = 0;
        fill_eytzinger(entries, sorted_idx, 1);
    }

    const Value* find(int64_t target) const {
        if (n == 0) return nullptr;

        size_t i = 1;
        while (i <= n) {
            __builtin_prefetch(&keys[2 * i]);
            i = 2 * i + (keys[i] < target);
        }

        // i is now past a leaf — walk back up to find the answer.
        // After the branchless descent, the answer is at i>>ffs(~i),
        // which undoes the last "go right" step.
        i >>= __builtin_ffs(static_cast<int>(~i));
        if (i > 0 && i <= n && keys[i] == target)
            return &vals[i];
        return nullptr;
    }

private:
    void fill_eytzinger(const std::vector<std::pair<int64_t, Value>>& sorted,
                        size_t& sorted_idx, size_t tree_idx) {
        if (tree_idx > n) return;
        fill_eytzinger(sorted, sorted_idx, 2 * tree_idx);      // left child
        keys[tree_idx] = sorted[sorted_idx].first;
        vals[tree_idx] = sorted[sorted_idx].second;
        ++sorted_idx;
        fill_eytzinger(sorted, sorted_idx, 2 * tree_idx + 1);  // right child
    }
};

} // namespace llti
