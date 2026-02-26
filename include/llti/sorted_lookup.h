#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace llti {

template <typename Value>
struct SortedLookup {
    std::vector<int64_t> keys;
    std::vector<Value> vals;

    void build(std::vector<std::pair<int64_t, Value>> entries) {
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        keys.reserve(entries.size());
        vals.reserve(entries.size());
        for (auto& [k, v] : entries) {
            keys.push_back(k);
            vals.push_back(std::move(v));
        }
    }

    const Value* find(int64_t target) const {
        auto it = std::lower_bound(keys.begin(), keys.end(), target);
        if (it != keys.end() && *it == target)
            return &vals[it - keys.begin()];
        return nullptr;
    }
};

} // namespace llti
