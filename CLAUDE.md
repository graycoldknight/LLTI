# CLAUDE.md

## Project Overview

LLTI (Low Latency Trading Insights) explores performance-sensitive data structures for static lookup workloads. The motivating question: *Given 10M static 64-bit keys, how do we minimize per-lookup latency when data fits in RAM but not L2?*

## Build Commands

```bash
mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make
```

## Running Tests

```bash
./build/llti_tests
```

Uses Google Test (v1.14.0, fetched via CMake FetchContent).

## Running Benchmarks

```bash
./build/llti_benchmarks
./build/llti_benchmarks --benchmark_filter=BM_SortedLookup_10M
./build/llti_benchmarks --benchmark_filter=BM_EytzingerLookup_10M
```

Uses Google Benchmark (v1.8.3, fetched via CMake FetchContent).

## Benchmark Results (WSL2, 10M int64_t keys)

| Layout | Lookup Latency | vs Baseline |
|--------|---------------|-------------|
| Sorted + `std::lower_bound` | 292 ns | baseline |
| Eytzinger (BFS) + branchless | 145 ns | **2.0x faster** |

## Architecture

Header-only library in `include/llti/`.

- **`sorted_lookup.h`** — Naive sorted array with `std::lower_bound` binary search. Baseline implementation.
- **`eytzinger_lookup.h`** — Eytzinger (BFS) layout with branchless search and software prefetch. Keys are stored in breadth-first order of an implicit binary tree (1-indexed: node `i` has children `2i`, `2i+1`). The search loop is branchless: `i = 2*i + (keys[i] < target)` with `__builtin_prefetch` to hide memory latency. After descent, the answer is recovered via `i >>= __builtin_ffs(~i)`. Build uses in-order recursive fill from sorted input.

## Performance Tuning Skills

The `skills/perf/` submodule provides performance analysis skills:

| Skill | Use When |
|-------|----------|
| `/perf-tma-tuning` | Analyzing bottleneck buckets (Frontend Bound, Backend Bound, Bad Speculation, Retiring) |
| `/perf-xpedite-tuning` | Comparing code variants or measuring per-transaction latency |
| `/perf-branch-tuning` | Optimizing branch prediction or implementing branchless code |
| `/perf-memory-tuning` | Improving cache hit rates, data locality, or reducing TLB misses |
| `/perf-code-layout-tuning` | Reducing I-cache misses or ITLB pressure |

## Testing

- **7 tests** in `tests/lookup_test.cpp` — SortedLookup correctness
- **9 tests** in `tests/eytzinger_test.cpp` — EytzingerLookup correctness including power-of-two, non-power-of-two sizes, and 100K random dataset

Run a single test suite: `./build/llti_tests --gtest_filter='EytzingerLookup*'`

## Key Conventions

- C++17, header-only library
- Template parameterization on value type (`SortedLookup<Value>`, `EytzingerLookup<Value>`)
- 10M int64_t keys is the standard benchmark size
- Benchmarks use 1024-element lookup key batch to simulate cache-cold random access
