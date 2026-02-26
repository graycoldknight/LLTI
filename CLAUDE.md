# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

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
./build/llti_benchmarks --benchmark_filter=BM_VebLookup_10M
```

Uses Google Benchmark (v1.8.3, fetched via CMake FetchContent).

## AWS Benchmarking (benchmark_c7i.sh)

```bash
./benchmark_c7i.sh                                        # Standard benchmarks (Clang 16, -march=native)
./benchmark_c7i.sh --filter='BM_EytzingerLookup_10M'     # Single benchmark
./benchmark_c7i.sh --repetitions=20                        # More repetitions (default: 10)
./benchmark_c7i.sh --fix-perf                              # Fix broken perf on newer AWS kernels
./benchmark_c7i.sh --setup-isolation                       # Configure isolcpus=1 (c7i.large, requires reboot)
./benchmark_c7i.sh --setup-isolation=c7i.xlarge            # Configure isolcpus=1,3 (c7i.xlarge)
./benchmark_c7i.sh --topdown                               # TMA L1 via toplev.py
./benchmark_c7i.sh --toplev                                # TMA L2 via toplev.py (default depth 2)
./benchmark_c7i.sh --toplev --toplev-level=3               # TMA L3
./benchmark_c7i.sh --toplev --filter='BM_SortedLookup_10M' # TMA on specific benchmark
```

Always does a clean rebuild with Clang 16. Benchmarks run with `taskset -c 1` on isolated cores. Uses `toplev.py --force-cpu spr` from pmu-tools (cloned to `third_party/pmu-tools`) since AWS kernels lack topdown PMU support.

## Benchmark Results (10M int64_t keys)

### WSL2 (Local)
| Layout | Lookup Latency | vs Baseline |
|--------|---------------|-------------|
| Sorted + `std::lower_bound` | 292 ns | baseline |
| Eytzinger (BFS) + branchless | 145 ns | **2.0x faster** |

### AWS c7i (Intel Sapphire Rapids, Isolated Core)
| Layout | Lookup Latency | vs Baseline |
|--------|---------------|-------------|
| Sorted + `std::lower_bound` | 322 ns | baseline |
| Eytzinger (BFS) + branchless | 76.1 ns | **4.2x faster** |

## Architecture

Header-only library in `include/llti/`.

- **`sorted_lookup.h`** — Naive sorted array with `std::lower_bound` binary search. Baseline implementation.
- **`eytzinger_lookup.h`** — Eytzinger (BFS) layout with branchless search and software prefetch. Keys are stored in breadth-first order of an implicit binary tree (1-indexed: node `i` has children `2i`, `2i+1`). The search loop is branchless: `i = 2*i + (keys[i] < target)` with `__builtin_prefetch` to hide memory latency. After descent, the answer is recovered via `i >>= __builtin_ffs(~i)`. Build uses in-order recursive fill from sorted input.
- **`veb_lookup.h`** — van Emde Boas (vEB) cache-oblivious layout. Recursively splits the binary tree into top/bottom subtrees. Uses explicit child pointers packed into a 16-byte aligned `SearchData` struct (key + left/right indices). Search is branching (not branchless). Build maps BFS indices to vEB order via recursive layout generation.

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
- **9 tests** in `tests/veb_test.cpp` — VebLookup correctness (same coverage pattern as Eytzinger tests)

Run a single test suite: `./build/llti_tests --gtest_filter='EytzingerLookup*'`

## Key Conventions

- C++17, header-only library
- Template parameterization on value type (`SortedLookup<Value>`, `EytzingerLookup<Value>`)
- 10M int64_t keys is the standard benchmark size
- Benchmarks use 1024-element lookup key batch to simulate cache-cold random access
