# LLTI — Low Latency Trading Insights

Exploring cache-oblivious and cache-aware data structures for static lookup workloads.

**Motivating question:** Given 10M static 64-bit keys, how do we minimize per-lookup latency when data fits in RAM but not L2?

## Quick Start

```bash
mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make
./tests/llti_tests
./benchmarks/llti_benchmarks
```

## Data Structures

| Layout | Description | Status |
|--------|-------------|--------|
| Sorted array + `std::lower_bound` | Naive baseline | Done |
| Eytzinger (BFS) layout | Cache-oblivious binary search | Planned |
| B-tree layout | Cache-line-aligned nodes | Planned |

## Performance Skills

The `skills/perf/` submodule provides Claude Code skills for TMA, Xpedite profiling, and micro-optimization guidance.
