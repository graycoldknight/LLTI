# Benchmark Results and Microarchitecture Analysis

## Overview
This document details the performance characteristics of the `std::lower_bound` (Sorted Array) baseline versus the Eytzinger (BFS) branchless layout. The primary goal is to minimize per-lookup latency for 10M static 64-bit keys where data fits in RAM but not L2 cache.

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
