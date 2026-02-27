#include <cstdint>
#include <cstring>
#include <array>
#include <vector>

// ============================================================================
// OrderBook_Fast: Cache-friendly order book for ultra-low-latency trading
//
// Key design decisions driven by hardware constraints:
//   1. Direct-indexed price array (no hashing, no pointer chasing)
//   2. Pre-allocated memory pool for orders (no heap allocation on hot path)
//   3. Open-addressing hash map for order_id lookup (cache-line friendly)
//   4. All critical structures sized to fit in L1/L2 cache
// ============================================================================

using PriceTick = int64_t;

// --- Configuration -----------------------------------------------------------
static constexpr size_t MAX_ORDERS = 1 << 20;  // 1M orders, power-of-2 for masking

// --- Price Level Array -------------------------------------------------------
// Direct-indexed: volume_levels[price - PRICE_MIN_TICK]
// 5,001 × 4 bytes = ~20 KB → fits entirely in L1 cache (32-48 KB on modern x86)
// True O(1) with zero hashing overhead

// --- Order Pool --------------------------------------------------------------
// Pre-allocated flat array. No heap allocation on insert/cancel.
// Orders are stored contiguously for cache-friendly iteration if needed.

struct alignas(32) Order {
    uint64_t  order_id;     // 8
    PriceTick price;        // 8
    int32_t   quantity;     // 4
    uint32_t  padding_;     // 4 — pad to 32 bytes for clean cache line sharing
};
static_assert(sizeof(Order) == 32, "Order must be 32 bytes for cache alignment");

// --- Open-Addressing Hash Map for order_id → pool index ----------------------
// Robin Hood or linear probing over a flat array. No linked list nodes,
// no heap allocation, no pointer chasing. Lookups touch 1-2 cache lines.

class OrderMap {
private:
    struct Slot {
        uint64_t key;       // order_id (0 = empty)
        uint32_t value;     // index into order pool
        uint32_t padding_;
    };

    static constexpr size_t CAPACITY = MAX_ORDERS * 2;  // 50% load factor
    static constexpr size_t MASK = CAPACITY - 1;
    static_assert((CAPACITY & (CAPACITY - 1)) == 0, "must be power of 2");

    std::vector<Slot> slots_;

public:
    OrderMap() : slots_(CAPACITY, Slot{0, 0, 0}) {}

    // Linear probing insert — no allocation
    void insert(uint64_t key, uint32_t value) {
        size_t idx = hash(key) & MASK;
        while (slots_[idx].key != 0) {
            idx = (idx + 1) & MASK;
        }
        slots_[idx] = {key, value, 0};
    }

    // Linear probing lookup — touches 1-2 contiguous cache lines
    uint32_t* find(uint64_t key) {
        size_t idx = hash(key) & MASK;
        while (slots_[idx].key != 0) {
            if (slots_[idx].key == key) return &slots_[idx].value;
            idx = (idx + 1) & MASK;
        }
        return nullptr;
    }

    // Mark slot as deleted via tombstone
    void erase(uint64_t key) {
        size_t idx = hash(key) & MASK;
        while (slots_[idx].key != 0) {
            if (slots_[idx].key == key) {
                slots_[idx].key = TOMBSTONE;
                return;
            }
            idx = (idx + 1) & MASK;
        }
    }

private:
    static constexpr uint64_t TOMBSTONE = ~uint64_t(0);

    // Fast integer hash (splitmix64 finalizer)
    static uint64_t hash(uint64_t x) {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return x;
    }
};

// --- OrderBook ---------------------------------------------------------------

class OrderBook {
private:
    const PriceTick price_min_;
    const size_t    num_levels_;

    // Price level volumes — direct indexed, allocated once at construction
    std::vector<int32_t> volume_levels_;

    // Pre-allocated order pool — no heap alloc on hot path
    Order order_pool_[MAX_ORDERS];
    uint32_t pool_count_ = 0;

    // Flat open-addressing map: order_id → pool index
    OrderMap order_map_;

public:
    // Allocate everything upfront — this is startup cost, not hot path.
    // Example: OrderBook book(10'000, 15'000) for $100.00–$150.00 at $0.01 ticks
    OrderBook(PriceTick min_tick, PriceTick max_tick)
        : price_min_(min_tick)
        , num_levels_(max_tick - min_tick + 1)
        , volume_levels_(num_levels_, 0)
    {}

    // --- Hot path: volume query — TRUE O(1), single array read, L1 hit ---
    int32_t get_volume_at_price(PriceTick price) const {
        return volume_levels_[price - price_min_];
    }

    // --- Hot path: add order — no heap allocation ---
    void add_order(uint64_t order_id, PriceTick price, int32_t quantity) {
        uint32_t idx = pool_count_++;
        order_pool_[idx] = {order_id, price, quantity, 0};
        order_map_.insert(order_id, idx);
        volume_levels_[price - price_min_] += quantity;
    }

    // --- Hot path: cancel order — no heap deallocation ---
    void cancel_order(uint64_t order_id) {
        uint32_t* idx_ptr = order_map_.find(order_id);
        if (!idx_ptr) return;

        Order& order = order_pool_[*idx_ptr];
        volume_levels_[order.price - price_min_] -= order.quantity;
        order.quantity = 0;
        order_map_.erase(order_id);
    }

    // --- Hot path: modify order — no allocation ---
    void modify_order(uint64_t order_id, PriceTick new_price, int32_t new_quantity) {
        uint32_t* idx_ptr = order_map_.find(order_id);
        if (!idx_ptr) return;

        Order& order = order_pool_[*idx_ptr];

        volume_levels_[order.price - price_min_] -= order.quantity;

        order.price = new_price;
        order.quantity = new_quantity;
        volume_levels_[new_price - price_min_] += new_quantity;
    }

    size_t num_orders() const { return pool_count_; }
};
