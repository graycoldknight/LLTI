#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace llti {

// van Emde Boas (vEB) layout for cache-oblivious binary search.
//
// This splits the binary tree recursively into top and bottom subtrees.
// Unlike Eytzinger which is implicit, we use explicit child indices.
// To maximize cache efficiency, we pack the key and child indices into a
// single 16-byte structure (Array of Structs).
// The int64_t key type is consistent with SortedLookup and EytzingerLookup.

template <typename Value>
struct VebLookup {
    struct alignas(16) SearchData {
        int64_t key;
        uint32_t children[2]; // [0]=left, [1]=right
    };

    std::vector<SearchData> tree;
    std::vector<Value> vals;
    size_t n = 0;
    uint32_t root_idx = 0;

    void build(std::vector<std::pair<int64_t, Value>> entries) {
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        n = entries.size();
        if (n == 0) return;

        if (n + 1 > std::numeric_limits<uint32_t>::max()) {
            throw std::overflow_error("VebLookup: n exceeds uint32_t index range");
        }

        // __builtin_clzll is undefined for 0; guarded by n == 0 check above.
        int h = 64 - __builtin_clzll(n); // ceil(log2(N)) + 1

        // The following 5 temp vectors are a one-time build cost for static data.
        // Build is O(N) memory and amortized across all subsequent lookups.
        std::vector<size_t> veb_order;
        veb_order.reserve(n);
        build_veb_complete(1, h, n, veb_order);

        std::vector<size_t> bfs_to_veb(n + 1);
        for (size_t i = 0; i < n; ++i) {
            bfs_to_veb[veb_order[i]] = i + 1; // 1-based index
        }

        std::vector<size_t> inorder_bfs;
        inorder_bfs.reserve(n);
        inorder_complete(1, n, inorder_bfs);

        std::vector<size_t> bfs_to_sorted(n + 1);
        for (size_t i = 0; i < n; ++i) {
            bfs_to_sorted[inorder_bfs[i]] = i;
        }

        tree.resize(n + 1);
        vals.resize(n + 1);

        for (size_t bfs = 1; bfs <= n; ++bfs) {
            size_t veb_idx = bfs_to_veb[bfs];
            size_t sorted_idx = bfs_to_sorted[bfs];

            tree[veb_idx].key = entries[sorted_idx].first;
            vals[veb_idx] = entries[sorted_idx].second;

            size_t left_bfs = 2 * bfs;
            size_t right_bfs = 2 * bfs + 1;

            tree[veb_idx].children[0] = static_cast<uint32_t>(
                (left_bfs <= n) ? bfs_to_veb[left_bfs] : 0);
            tree[veb_idx].children[1] = static_cast<uint32_t>(
                (right_bfs <= n) ? bfs_to_veb[right_bfs] : 0);
        }

        root_idx = static_cast<uint32_t>(bfs_to_veb[1]);
    }

    const Value* find(int64_t target) const {
        if (n == 0) return nullptr;

        uint32_t curr = root_idx;
        uint32_t candidate = 0;

        while (curr != 0) {
            __builtin_prefetch(&tree[tree[curr].children[0]]);
            __builtin_prefetch(&tree[tree[curr].children[1]]);
            int64_t key = tree[curr].key;
            candidate = (target <= key) ? curr : candidate;  // CMOV
            curr = tree[curr].children[key < target];         // branchless select
        }

        if (candidate != 0 && tree[candidate].key == target) {
            return &vals[candidate];
        }
        return nullptr;
    }

private:
    void build_veb_complete(size_t bfs_idx, int h, size_t N, std::vector<size_t>& veb_order) {
        if (h == 0 || bfs_idx > N) return;
        if (h == 1) {
            veb_order.push_back(bfs_idx);
            return;
        }
        int bottom_h = h / 2;
        int top_h = h - bottom_h;

        build_veb_complete(bfs_idx, top_h, N, veb_order);

        size_t num_bottom = size_t{1} << top_h;
        size_t first_leaf_bfs = bfs_idx * (size_t{1} << top_h);
        for (size_t i = 0; i < num_bottom; ++i) {
            if (first_leaf_bfs + i > N) break;
            build_veb_complete(first_leaf_bfs + i, bottom_h, N, veb_order);
        }
    }

    void inorder_complete(size_t bfs_idx, size_t N, std::vector<size_t>& inorder_bfs) {
        if (bfs_idx > N) return;
        inorder_complete(2 * bfs_idx, N, inorder_bfs);
        inorder_bfs.push_back(bfs_idx);
        inorder_complete(2 * bfs_idx + 1, N, inorder_bfs);
    }
};

} // namespace llti
