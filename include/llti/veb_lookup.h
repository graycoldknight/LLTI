#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace llti {

// van Emde Boas (vEB) layout for cache-oblivious binary search.
//
// This splits the binary tree recursively into top and bottom subtrees.
// Unlike Eytzinger which is implicit, we use explicit left/right indices.
// To maximize cache efficiency, we pack the key and child indices into a
// single 16-byte structure (Structure of Arrays).

template <typename Value>
struct VebLookup {
    struct alignas(16) SearchData {
        int64_t key;
        uint32_t left;
        uint32_t right;
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

        int h = 64 - __builtin_clzll(n); // log2(N) + 1

        std::vector<int> veb_order;
        veb_order.reserve(n);
        build_veb_complete(1, h, n, veb_order);

        std::vector<int> bfs_to_veb(n + 1);
        for (size_t i = 0; i < n; ++i) {
            bfs_to_veb[veb_order[i]] = i + 1; // 1-based index
        }

        std::vector<int> inorder_bfs;
        inorder_bfs.reserve(n);
        inorder_complete(1, n, inorder_bfs);

        std::vector<int> bfs_to_sorted(n + 1);
        for (size_t i = 0; i < n; ++i) {
            bfs_to_sorted[inorder_bfs[i]] = i;
        }

        tree.resize(n + 1);
        vals.resize(n + 1);

        for (int bfs = 1; bfs <= static_cast<int>(n); ++bfs) {
            int veb_idx = bfs_to_veb[bfs];
            int sorted_idx = bfs_to_sorted[bfs];

            tree[veb_idx].key = entries[sorted_idx].first;
            vals[veb_idx] = entries[sorted_idx].second;

            int left_bfs = 2 * bfs;
            int right_bfs = 2 * bfs + 1;

            tree[veb_idx].left = (left_bfs <= static_cast<int>(n)) ? bfs_to_veb[left_bfs] : 0;
            tree[veb_idx].right = (right_bfs <= static_cast<int>(n)) ? bfs_to_veb[right_bfs] : 0;
        }

        root_idx = bfs_to_veb[1];
    }

    const Value* find(int64_t target) const {
        if (n == 0) return nullptr;

        uint32_t curr = root_idx;
        uint32_t candidate = 0;

        while (curr != 0) {
            int64_t key = tree[curr].key;
            if (target <= key) {
                candidate = curr;
                curr = tree[curr].left;
            } else {
                curr = tree[curr].right;
            }
        }

        if (candidate != 0 && tree[candidate].key == target) {
            return &vals[candidate];
        }
        return nullptr;
    }

private:
    void build_veb_complete(int bfs_idx, int h, int N, std::vector<int>& veb_order) {
        if (h == 0 || bfs_idx > N) return;
        if (h == 1) {
            veb_order.push_back(bfs_idx);
            return;
        }
        int bottom_h = h / 2;
        int top_h = h - bottom_h;

        build_veb_complete(bfs_idx, top_h, N, veb_order);

        int num_bottom = 1 << top_h;
        int first_leaf_bfs = bfs_idx * (1 << top_h);
        for (int i = 0; i < num_bottom; ++i) {
            if (first_leaf_bfs + i > N) break;
            build_veb_complete(first_leaf_bfs + i, bottom_h, N, veb_order);
        }
    }

    void inorder_complete(int bfs_idx, int N, std::vector<int>& inorder_bfs) {
        if (bfs_idx > N) return;
        inorder_complete(2 * bfs_idx, N, inorder_bfs);
        inorder_bfs.push_back(bfs_idx);
        inorder_complete(2 * bfs_idx + 1, N, inorder_bfs);
    }
};

} // namespace llti
