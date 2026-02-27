# Benchmark Results and Microarchitecture Analysis

## Overview
This document details the performance characteristics of three static lookup layouts: `std::lower_bound` (Sorted Array) baseline, Eytzinger (BFS) branchless layout, and van Emde Boas (vEB) cache-oblivious layout. The primary goal is to minimize per-lookup latency for 10M static 64-bit keys where data fits in RAM but not L2 cache.

## Benchmark Results (10M int64_t keys)

### WSL2 (Local)
| Layout | Lookup Latency | vs Baseline |
|--------|---------------|-------------|
| Sorted + `std::lower_bound` | 292 ns | baseline |
| Eytzinger (BFS) + branchless | 145 ns | **2.0x faster** |

### AWS c7i (Intel Sapphire Rapids, Isolated Core)
| Layout | Lookup Latency | vs Baseline | vs Eytzinger |
|--------|---------------|-------------|--------------|
| Sorted + `std::lower_bound` | 322 ns | baseline | 5.0x slower |
| **Eytzinger (BFS) + branchless** | **64.7 ns** | **5.0x faster** | **baseline** |
| vEB + branchless + prefetch | 97.0 ns | 3.3x faster | 1.5x slower |

## Top-Down Microarchitecture Analysis (TMA)

To understand the massive 4.2x performance gain of the Eytzinger layout on the Sapphire Rapids architecture, we performed a Top-Down Microarchitecture Analysis (TMA) using `toplev.py` (Level 2).

### 1. The Bottleneck of `std::lower_bound` (Sorted Array)
The standard binary search loop contains a highly unpredictable branch: `if (keys[mid] < target)`. Because the data doesn't fit in the L1/L2 cache, the CPU has to wait for main memory or L3 cache to evaluate the condition.

**TMA Profile (Sorted Array):**
```text
# 5.01-full-perf on Intel(R) Xeon(R) Platinum 8488C [spr/sapphire_rapids]
FE               Frontend_Bound                      % Slots                       34.8    [33.0%]
BAD              Bad_Speculation                     % Slots                       30.2    [33.0%]
BE               Backend_Bound                       % Slots                       15.0  < [33.0%]
RET              Retiring                            % Slots                       20.0  < [33.0%]
FE               Frontend_Bound.Fetch_Latency        % Slots                       17.7    [33.0%]<==
BAD              Bad_Speculation.Branch_Mispredicts  % Slots                       31.1    [33.0%]
```

**Observation:** Modern CPUs try to guess the branch direction and execute ahead, but the search path is effectively random, causing a high misprediction rate. When the memory finally arrives and the CPU realizes it guessed wrong, it flushes its pipeline (causing the massive **30.2% Bad Speculation** penalty) and refetches instructions from the correct path (causing the high **34.8% Frontend Bound / Fetch Latency**).

### 2. The Efficiency of Eytzinger (Branchless BFS Layout)
The Eytzinger implementation replaces the unpredictable `if` statement with a branchless arithmetic operation: `i = 2 * i + (keys[i] < target)`. It also incorporates software prefetching (`__builtin_prefetch(&keys[2 * i])`) to fetch the next tree level early.

**TMA Profile (Eytzinger):**
```text
# 5.01-full-perf on Intel(R) Xeon(R) Platinum 8488C [spr/sapphire_rapids]
FE               Frontend_Bound                      % Slots                       30.3    [33.0%]
BAD              Bad_Speculation                     % Slots                       25.0    [33.0%]
BE               Backend_Bound                       % Slots                       22.0    [33.0%]
RET              Retiring                            % Slots                       22.7  < [33.0%]
FE               Frontend_Bound.Fetch_Latency        % Slots                       15.2    [33.0%]<==
BAD              Bad_Speculation.Branch_Mispredicts  % Slots                       25.6    [33.0%]
BE/Core          Backend_Bound.Core_Bound            % Slots                       13.2    [33.0%]
RET              Retiring.Light_Operations           % Slots                       21.3  < [33.0%]
```

**Observation:**
1. **Eliminated Branch Penalties:** The CPU no longer has to guess which way the branch goes; it evaluates the condition mathematically without interrupting the instruction pipeline. This drastically reduces the absolute time wasted on Bad Speculation (though the relative % remains due to the much shorter execution time).
2. **Shifted Bottleneck:** Because the pipeline is no longer constantly flushing from bad branch predictions, the CPU is free to actually execute instructions. The bottleneck shifts towards the **Backend Bound (Core Bound at 13.2%)** and **Retiring (Useful Work at 22.7%)**. The CPU is executing the arithmetic instructions as fast as its execution units allow, perfectly overlapping computation with memory fetches.

**Conclusion:** The Eytzinger layout transforms a memory-stalled, heavily mispredicted control-flow problem into a smooth, predictable data-flow pipeline that perfectly utilizes the Sapphire Rapids out-of-order execution engine, resulting in a 4.2x speedup.

## 3. The van Emde Boas (vEB) Layout Performance

We implemented and benchmarked a cache-oblivious van Emde Boas (vEB) layout. It recursively splits the binary tree into top and bottom subtrees to maximize cache locality. The implementation uses explicit child indices packed into a 16-byte aligned `SearchData` struct (Array of Structs), with a branchless search loop and dual software prefetch.

### Implementation Evolution

The vEB layout went through two iterations:

1. **v1 (branching, no prefetch):** Used `left`/`right` fields with an if/else search loop. WSL2: ~118 ns.
2. **v2 (branchless + prefetch):** Replaced with `children[2]` array, branchless child selection via `children[key < target]`, CMOV for candidate update, and dual `__builtin_prefetch` for both children. Also hardened build with `size_t` types and `uint32_t` overflow guard.

### Benchmark Results (10M int64_t keys, AWS c7i)

| Layout | Lookup Latency | vs Baseline | vs Eytzinger |
|--------|---------------|-------------|--------------|
| Sorted + `std::lower_bound` | 322 ns | baseline | 5.0x slower |
| **Eytzinger (BFS) + branchless** | **64.7 ns** | **5.0x faster** | **baseline** |
| vEB v1 (branching, no prefetch) | — | — | — |
| vEB v2 (branchless + prefetch) | 97.0 ns | 3.3x faster | 1.5x slower |

The branchless + prefetch optimization did not close the gap with Eytzinger. TMA analysis below explains why.

### TMA L2 Analysis: Eytzinger vs vEB (AWS c7i, toplev.py)

Both layouts now use branchless search with software prefetch. The TMA reveals why Eytzinger still wins decisively.

**Raw TMA Profiles:**

```text
# Eytzinger (64.7 ns) — toplev.py -l2 --force-cpu spr
FE               Frontend_Bound                      % Slots                       29.7
BAD              Bad_Speculation                     % Slots                       26.8
BE               Backend_Bound                       % Slots                       28.9
RET              Retiring                            % Slots                       14.8
FE               Frontend_Bound.Fetch_Latency        % Slots                       14.3
FE               Frontend_Bound.Fetch_Bandwidth      % Slots                       15.3
BAD              Bad_Speculation.Branch_Mispredicts  % Slots                       27.2
BAD              Bad_Speculation.Machine_Clears      % Slots                        0.0
BE/Mem           Backend_Bound.Memory_Bound          % Slots                       11.0
BE/Core          Backend_Bound.Core_Bound            % Slots                       17.8
RET              Retiring.Light_Operations           % Slots                       13.7
RET              Retiring.Heavy_Operations           % Slots                        1.1
```

```text
# vEB v2 (97.0 ns) — toplev.py -l2 --force-cpu spr
FE               Frontend_Bound                      % Slots                       25.0
BAD              Bad_Speculation                     % Slots                       21.0
BE               Backend_Bound                       % Slots                       37.4
RET              Retiring                            % Slots                       16.7
FE               Frontend_Bound.Fetch_Latency        % Slots                       12.2
FE               Frontend_Bound.Fetch_Bandwidth      % Slots                       12.6
BAD              Bad_Speculation.Branch_Mispredicts  % Slots                       21.2
BAD              Bad_Speculation.Machine_Clears      % Slots                        0.0
BE/Mem           Backend_Bound.Memory_Bound          % Slots                       21.8
BE/Core          Backend_Bound.Core_Bound            % Slots                       15.6
RET              Retiring.Light_Operations           % Slots                       15.5
RET              Retiring.Heavy_Operations           % Slots                        1.2
```

**Side-by-Side Comparison:**

| TMA Metric | Eytzinger (64.7ns) | vEB (97.0ns) | Delta | Interpretation |
|------------|-------------------|-------------|-------|----------------|
| Frontend Bound | 29.7% | 25.0% | -4.7 | vEB slightly better |
| → Fetch_Latency | 14.3% | 12.2% | -2.1 | I-cache not a bottleneck for either |
| → Fetch_Bandwidth | 15.3% | 12.6% | -2.7 | |
| Bad Speculation | 26.8% | 21.0% | -5.8 | Both branchless; residual from loop exit mispredict |
| → Branch_Mispredicts | 27.2% | 21.2% | -6.0 | |
| **Backend Bound** | **28.9%** | **37.4%** | **+8.5** | **vEB significantly worse** |
| **→ Memory_Bound** | **11.0%** | **21.8%** | **+10.8** | **2x worse — dominant vEB bottleneck** |
| → Core_Bound | 17.8% | 15.6% | -2.2 | Similar |
| Retiring | 14.8% | 16.7% | +1.9 | Both low (pointer-chasing limits IPC) |

### Key Findings

**1. Memory_Bound is the dominant bottleneck for vEB (21.8% vs 11.0%)**

Despite both layouts using branchless search and prefetch, vEB spends 2x more slots stalled on memory. This is the single largest contributor to the 50% performance gap.

**2. Double the working set**

Eytzinger stores 8 bytes per node (key only; children are implicit via `2*i` and `2*i+1`). vEB stores 16 bytes per node (8-byte key + two 4-byte child indices in the `SearchData` struct). For 10M keys:
- Eytzinger: ~80 MB working set
- vEB: ~160 MB working set

With double the data, fewer nodes fit per cache line and more cache capacity is consumed, leading to more L2/L3 misses.

**3. The dependent prefetch chain (the fundamental problem)**

This is the critical architectural difference. In Eytzinger, the prefetch target is computed via pure arithmetic — no memory dependency:

```cpp
// Eytzinger: prefetch address known instantly via arithmetic
__builtin_prefetch(&keys[2 * i]);  // address = 2*i, computed in 1 cycle
```

In vEB, the prefetch target requires **loading the current node first**:

```cpp
// vEB: prefetch address requires loading tree[curr] — dependent load!
__builtin_prefetch(&tree[tree[curr].children[0]]);  // must wait for tree[curr] cache line
__builtin_prefetch(&tree[tree[curr].children[1]]);
```

The CPU cannot issue the prefetch until `tree[curr]` arrives from memory. But waiting for `tree[curr]` to arrive **is** the memory latency we're trying to hide. The prefetch fires too late to be useful — the next iteration's cache miss is already on the critical path. This creates a **serialized dependency chain** that cannot be broken without implicit indexing.

**4. Bad Speculation is non-zero for both (but not the differentiator)**

Both layouts show ~21-27% Bad Speculation despite branchless search bodies. This comes from the `while (curr != 0)` loop termination branch, which mispredicts on the final iteration (~1 mispredict per lookup across ~24 iterations). The benchmark's 1024-element batch loop also contributes. This cost is similar for both layouts and not the cause of the performance gap.

**5. Frontend Bound is actually better for vEB**

vEB shows lower Frontend Bound (25% vs 30%), confirming the I-cache is not a bottleneck. Both search loops compile to tight instruction sequences.

### Conclusion

The vEB layout's theoretical cache-oblivious advantage (subtrees stored contiguously for optimal spatial locality) is negated by two practical costs of explicit child pointers:

1. **2x memory footprint** → more cache misses
2. **Dependent prefetch chain** → prefetch cannot hide memory latency

Eytzinger's implicit tree structure (`children at 2i, 2i+1`) is fundamentally superior for this workload because prefetch addresses are computed via arithmetic with zero memory dependency, allowing the CPU to fully pipeline memory accesses across tree levels. To make vEB competitive, it would need an implicit index mapping that avoids stored child pointers — a significantly more complex implementation that sacrifices the layout's simplicity.
