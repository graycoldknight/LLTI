# LLTI — Low Latency Trading Insights

LLTI is a specialized C++17 project dedicated to researching and implementing high-performance, cache-efficient data structures for static lookup workloads, specifically targeting low-latency trading (LLT) environments.

## Project Overview

- **Focus:** Minimizing per-lookup latency for datasets that fit in RAM but exceed L2 cache capacity (e.g., 10M 64-bit keys).
- **Architecture:** Header-only library (`include/llti/`) providing template-based lookup structures.
- **Key Technologies:**
  - **C++17** for modern systems programming.
  - **CMake** as the build system.
  - **Google Test** for unit testing.
  - **Google Benchmark** for micro-benchmarking.
  - **Clang 16+** (preferred) with `-march=native` for optimal code generation.
  - **Intel TMA (Top-Down Microarchitecture Analysis)** via `toplev.py` for performance bottleneck identification.

## Data Structures

| Layout | Description | Status | Lookup Latency |
|--------|-------------|--------|----------------|
| `SortedLookup` | Baseline using `std::lower_bound` on a sorted array. | Done | ~292 ns (1.0x) |
| `EytzingerLookup` | Cache-oblivious binary search using BFS tree layout and branchless descent. | Done | ~145 ns (2.0x) |
| B-tree layout | Cache-line-aligned nodes to minimize memory fetches. | Planned | TBD |

### Eytzinger Layout Details
- **Storage:** Implicit binary tree in breadth-first order (1-indexed). Node `i` has children at `2i` and `2i+1`.
- **Search:** Branchless descent loop: `i = 2*i + (keys[i] < target)`.
- **Prefetch:** Uses `__builtin_prefetch` to hide memory latency during descent.
- **Recovery:** After descent, the final position is recovered via `i >>= __builtin_ffs(~i)`.
- **Build:** Recursive in-order fill from sorted input.

## Building and Running

### Standard Build
```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### Run Tests
```bash
./build/llti_tests
```

### Run Benchmarks
```bash
./build/llti_benchmarks
```

### High-Fidelity Benchmarking (AWS c7i / Sapphire Rapids)
The project includes a specialized script `benchmark_c7i.sh` for reproducible results on Intel Sapphire Rapids:
- **Basic:** `./benchmark_c7i.sh`
- **TMA Level 1:** `./benchmark_c7i.sh --topdown`
- **Core Isolation:** `./benchmark_c7i.sh --setup-isolation`

## Development Conventions

- **Performance First:** Prioritize branchless logic, software prefetching (`__builtin_prefetch`), and cache-line alignment.
- **Header-Only:** Core logic resides in `include/llti/`. Use templates to support various value types.
- **Testing:** Every new data structure must have comprehensive tests in `tests/` covering edge cases (empty, single element, duplicates, non-power-of-two sizes).
- **Benchmarking:** Use `benchmarks/lookup_benchmark.cpp` to compare new implementations against `SortedLookup` and `EytzingerLookup`.
- **Tuning Skills:** Specialized performance tuning guidance is available in `skills/perf/` (TMA, memory access, branch prediction).

## Performance Tuning Skills

Activate these skills for expert guidance on micro-optimization:
- `perf-tma-tuning`: TMA analysis for Intel CPUs.
- `perf-memory-tuning`: Cache utilization and data locality.
- `perf-branch-tuning`: Branchless programming and prediction optimization.
- `perf-code-layout-tuning`: Instruction cache and ITLB optimization.
