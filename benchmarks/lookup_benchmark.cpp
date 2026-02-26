#include "llti/eytzinger_lookup.h"
#include "llti/sorted_lookup.h"
#include <benchmark/benchmark.h>
#include <random>

// Shared setup: generate 10M random key-value pairs
static std::vector<std::pair<int64_t, int64_t>> make_entries(int64_t n, uint64_t seed = 42) {
    std::mt19937_64 rng(seed);
    std::vector<std::pair<int64_t, int64_t>> entries;
    entries.reserve(n);
    for (int64_t i = 0; i < n; ++i) {
        int64_t key = static_cast<int64_t>(rng());
        entries.push_back({key, key});
    }
    return entries;
}

// --- Sorted (baseline) ---

static void BM_SortedLookup_10M(benchmark::State& state) {
    constexpr int64_t N = 10'000'000;
    auto entries = make_entries(N);

    llti::SortedLookup<int64_t> table;
    table.build(std::move(entries));

    constexpr int BATCH = 1024;
    std::mt19937_64 rng(99);
    std::uniform_int_distribution<int64_t> dist(0, N - 1);
    std::vector<int64_t> lookup_keys(BATCH);
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

// --- Eytzinger ---

static void BM_EytzingerLookup_10M(benchmark::State& state) {
    constexpr int64_t N = 10'000'000;
    auto entries = make_entries(N);

    llti::EytzingerLookup<int64_t> table;
    table.build(std::move(entries));

    // Pre-generate lookup keys from the Eytzinger key array (all existing)
    constexpr int BATCH = 1024;
    std::mt19937_64 rng(99);
    std::uniform_int_distribution<size_t> dist(1, table.n);
    std::vector<int64_t> lookup_keys(BATCH);
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
BENCHMARK(BM_EytzingerLookup_10M);

// --- Build benchmarks ---

static void BM_SortedLookup_Build(benchmark::State& state) {
    const int64_t N = state.range(0);
    auto entries = make_entries(N);

    for (auto _ : state) {
        llti::SortedLookup<int64_t> table;
        auto copy = entries;
        table.build(std::move(copy));
        benchmark::DoNotOptimize(table);
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_SortedLookup_Build)->Arg(10'000'000);

static void BM_EytzingerLookup_Build(benchmark::State& state) {
    const int64_t N = state.range(0);
    auto entries = make_entries(N);

    for (auto _ : state) {
        llti::EytzingerLookup<int64_t> table;
        auto copy = entries;
        table.build(std::move(copy));
        benchmark::DoNotOptimize(table);
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_EytzingerLookup_Build)->Arg(10'000'000);
