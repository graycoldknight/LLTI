# The Evolution of an Order Book: From Naive to Low-Latency

## Chapter 1: The Textbook Version (`OrderBook1.cpp`)

This is what you'd write in an interview if nobody mentioned performance. A `vector<Order*>` of heap-allocated orders, each containing a `std::string symbol`, a `double price`, and a `bool is_active` flag. To query volume at a price, it does a **full linear scan**, dereferencing a pointer each time.

**Problems everywhere:**
- **Pointer chasing** — `vector<Order*>` means every element is a heap pointer. Iterating touches random memory, thrashing the cache.
- **Bloated struct** — `std::string` (32 bytes typically) sits next to hot fields. Each `Order` is 64+ bytes — at most 1 per cache line.
- **`double` price comparison** — `==` on floating-point is a classic bug waiting to happen (`0.1 + 0.2 != 0.3`).
- **`is_active` flag** — Dead orders still live in the array and burn cycles every scan.
- **O(n) query** — With 1M orders, every `get_volume_at_price` touches millions of cache lines.

---

## Chapter 2: Struct-of-Arrays (`OrderBook2.cpp`)

The first real performance insight: **data layout matters**. Instead of an array of structs (AoS), this version splits into separate parallel vectors — `prices[]`, `quantities[]`, `order_ids[]`, `symbols[]`, and a **bitmap** `active_flags[]` (packed `uint64_t` bitset).

**What improved:**
- **Cache-friendly scan** — The hot loop only touches `prices[]` and `quantities[]`, contiguous in memory. The prefetcher loads them efficiently. Cold fields (`order_ids`, `symbols`) aren't touched during queries.
- **Bitmap for active flags** — 64 orders per `uint64_t`. Massive memory savings vs. one `bool` per order.

**Still wrong:** Still O(n) scan per query (just faster). Still `double` prices. No add/cancel/modify API.

---

## Chapter 3: Algorithmic Leap (`OrderBook3.cpp`)

The design fundamentally changes. Instead of scanning orders, it **pre-aggregates** volume into a hash map keyed by price.

**Key changes:**
- **`PriceTick` (int64_t)** replaces `double` — integer ticks eliminate floating-point bugs.
- **`volume_at_price_` hash map** — Maintained incrementally on add/cancel/modify. `get_volume_at_price()` is now **O(1)**.
- **`orders_` hash map** — `unordered_map<uint64_t, Order>` for O(1) cancel/modify by order ID.
- **Lean struct** — Just `order_id`, `price`, `quantity`. No more `std::string`.

**The leap:** Query went from **O(n) to O(1)**. This is the single biggest improvement in the series.

**Still wrong:** `std::unordered_map` is node-based — every insert/cancel triggers `malloc`/`free`. Pointer chasing on lookup. In a hot trading loop, this is devastating.

---

## Chapter 4: Hardware-Conscious Design (`OrderBook4.cpp`)

This version was **received from another programmer** — a big architectural jump that replaces every stdlib container on the hot path with hardware-aware structures.

**Three big changes:**

1. **Direct-indexed price array** — `volume_levels_[price - price_min_]` is a flat `vector<int32_t>` indexed by tick offset. For a 5,000-tick range, that's ~20 KB — fits entirely in **L1 cache**. Volume query is a single array read, zero hashing overhead. True O(1).

2. **Pre-allocated order pool** — `Order order_pool_[MAX_ORDERS]` is a fixed-size flat array allocated at construction. `add_order()` just bumps a counter. **Zero heap allocation on the hot path.**

3. **Custom open-addressing hash map (`OrderMap`)** — Replaces `std::unordered_map` with linear probing over a flat array. Power-of-2 capacity with bitmask indexing. Splitmix64 hash. Lookups touch 1-2 contiguous cache lines instead of chasing linked-list pointers.

4. **Struct alignment** — `Order` is `alignas(32)` and `static_assert`ed to 32 bytes — two orders per cache line.

**Still wrong:** Pool is append-only (cancelled orders leave holes, never reclaimed). OrderMap tombstones accumulate forever.

---

## Chapter 5: Code Review & Adoption (`OrderBook5.cpp`)

Identical logic to OB4 — this is you **reviewing, understanding, and adopting** the code from the other programmer. The structure, algorithms, and data layouts are the same. The value of this step is owning the code: understanding *why* each decision was made before building on top of it.

---

## Chapter 6: Production Hardening (`OrderBook6.cpp`)

The final version fixes operational gaps that would bite in a real system running for hours under continuous add/cancel churn.

**Four fixes:**

1. **Free list for pool recycling** — A `freelist_[]` stack tracks cancelled pool indices. `alloc_index()` checks the free list first, only bumping `pool_count_` when empty. The pool no longer grows unboundedly.

2. **Tombstone reuse in OrderMap** — `insert()` tracks the first tombstone during probing and reuses it. Prevents unbounded tombstone accumulation and probe chain degradation.

3. **Bounds checking** — `assert_price_in_range()` validates tick bounds on every add/modify/query. Zero cost in release builds (`-DNDEBUG`).

4. **Accurate bookkeeping** — `num_orders()` returns `pool_count_ - freelist_top_` (live orders, not total-ever-allocated).

---

## Summary Table

| Version | Price Type | Volume Query | Order Lookup | Data Layout | Hot-Path Allocation | Key Change |
|---------|-----------|-------------|-------------|-------------|-------------------|------------|
| **OB1** | `double` | O(n) scan, pointer chasing | None (scan) | AoS, heap pointers | Every access | Baseline — textbook OOP |
| **OB2** | `double` | O(n) scan, cache-friendly | None (scan) | SoA, contiguous arrays + bitmap | None (read-only) | **Data layout: SoA + bitset** |
| **OB3** | `int64_t` tick | **O(1)** hash lookup | O(1) hash lookup | `unordered_map` (node-based) | `malloc`/`free` per op | **Algorithmic: pre-aggregated index** |
| **OB4** | `int64_t` tick | **O(1)** array index (L1) | O(1) open-addressing | Flat arrays, direct-indexed | **Zero** (pool + bump alloc) | **Hardware: L1 array, custom hashmap, pool** |
| **OB5** | `int64_t` tick | **O(1)** array index (L1) | O(1) open-addressing | Flat arrays, direct-indexed | **Zero** (pool + bump alloc) | Code review & adoption of OB4 |
| **OB6** | `int64_t` tick | **O(1)** array index (L1) | O(1) open-addressing | Flat arrays, direct-indexed | **Zero** (pool + free list) | **Robustness: free list, tombstone reuse, asserts** |

**The arc:** Textbook OOP (OB1) → data-oriented layout (OB2) → algorithmic breakthrough (OB3) → hardware-conscious design from a peer (OB4) → code adoption (OB5) → production hardening (OB6). Each step targets a different layer: memory layout, complexity class, cache hierarchy, and operational durability.
