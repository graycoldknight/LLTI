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
```

Uses Google Benchmark (v1.8.3, fetched via CMake FetchContent).

## Architecture

Header-only library in `include/llti/`.

- **`sorted_lookup.h`** — Naive sorted array with `std::lower_bound` binary search. Baseline for comparison against cache-oblivious layouts.

## Performance Tuning Skills

The `skills/perf/` submodule provides performance analysis skills:

| Skill | Use When |
|-------|----------|
| `/perf-tma-tuning` | Analyzing bottleneck buckets (Frontend Bound, Backend Bound, Bad Speculation, Retiring) |
| `/perf-xpedite-tuning` | Comparing code variants or measuring per-transaction latency |
| `/perf-branch-tuning` | Optimizing branch prediction or implementing branchless code |
| `/perf-memory-tuning` | Improving cache hit rates, data locality, or reducing TLB misses |
| `/perf-code-layout-tuning` | Reducing I-cache misses or ITLB pressure |

## Key Conventions

- C++17, header-only library
- Template parameterization on layout strategy for easy A/B comparison
- 10M int64_t keys is the standard benchmark size
