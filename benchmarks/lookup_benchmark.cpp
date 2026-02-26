#include "llti/sorted_lookup.h"
#include <benchmark/benchmark.h>
#include <random>

static void BM_SortedLookup_10M(benchmark::State& state) {
    constexpr int64_t N = 10'000'000;
    std::mt19937_64 rng(42);

    // Build table with 10M random keys
    std::vector<std::pair<int64_t, int64_t>> entries;
    entries.reserve(N);
    for (int64_t i = 0; i < N; ++i) {
        int64_t key = static_cast<int64_t>(rng());
        entries.push_back({key, key});
    }

    llti::SortedLookup<int64_t> table;
    table.build(std::move(entries));

    // Pre-generate random lookup keys (all existing)
    constexpr int BATCH = 1024;
    std::vector<int64_t> lookup_keys(BATCH);
    std::uniform_int_distribution<int64_t> dist(0, N - 1);
    for (int i = 0; i < BATCH; ++i) {
        lookup_keys[i] = table.keys[dist(rng)];
    }

    int idx = 0;
    for (auto _ : state) {
        auto* val = table.find(lookup_keys[idx]);
        benchmark::DoNotOptimize(val);
        idx = (idx + 1) & (BATCH - 1);
    }
}
BENCHMARK(BM_SortedLookup_10M);

static void BM_SortedLookup_Build(benchmark::State& state) {
    const int64_t N = state.range(0);
    std::mt19937_64 rng(42);

    std::vector<std::pair<int64_t, int64_t>> entries;
    entries.reserve(N);
    for (int64_t i = 0; i < N; ++i) {
        int64_t key = static_cast<int64_t>(rng());
        entries.push_back({key, key});
    }

    for (auto _ : state) {
        llti::SortedLookup<int64_t> table;
        auto copy = entries;
        table.build(std::move(copy));
        benchmark::DoNotOptimize(table);
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_SortedLookup_Build)->Arg(10'000'000);
