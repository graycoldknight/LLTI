#include "llti/sorted_lookup.h"
#include <chrono>
#include <cstdio>
#include <random>

int main() {
    constexpr int64_t N = 10'000'000;
    std::mt19937_64 rng(42);

    std::vector<std::pair<int64_t, int64_t>> entries;
    entries.reserve(N);
    for (int64_t i = 0; i < N; ++i) {
        int64_t key = static_cast<int64_t>(rng());
        entries.push_back({key, key});
    }

    llti::SortedLookup<int64_t> table;
    auto t0 = std::chrono::high_resolution_clock::now();
    table.build(std::move(entries));
    auto t1 = std::chrono::high_resolution_clock::now();
    double build_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("Built sorted lookup with %ld keys in %.1f ms\n", N, build_ms);

    // Random lookups
    constexpr int LOOKUPS = 1'000'000;
    std::uniform_int_distribution<int64_t> dist(0, N - 1);
    std::vector<int64_t> queries(LOOKUPS);
    for (int i = 0; i < LOOKUPS; ++i) {
        queries[i] = table.keys[dist(rng)];
    }

    int64_t sum = 0;
    auto t2 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < LOOKUPS; ++i) {
        auto* val = table.find(queries[i]);
        if (val) sum += *val;
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    double lookup_ns = std::chrono::duration<double, std::nano>(t3 - t2).count() / LOOKUPS;
    std::printf("%d lookups: %.1f ns/lookup (sum=%ld)\n", LOOKUPS, lookup_ns, sum);

    return 0;
}
