#include <cassert>
#include <cstdint>
#include <cstring>
#include <array>
#include <vector>


using PriceTick = int64_t;

// --- Configuration -----------------------------------------------------------
static constexpr size_t MAX_ORDERS = 1 << 20;  // 1M orders, power-of-2 for masking

// --- Order Pool --------------------------------------------------------------

struct alignas(32) Order {
    uint64_t  order_id;     // 8
    PriceTick price;        // 8
    int32_t   quantity;     // 4
    uint32_t  padding_;     // 4 — pad to 32 bytes for clean cache line sharing
};
static_assert(sizeof(Order) == 32, "Order must be 32 bytes for cache alignment");

// --- Open-Addressing Hash Map for order_id → pool index ----------------------
// Linear probing with tombstone reuse. Tombstone slots are reclaimed during
// insert, preventing unbounded tombstone accumulation.

class OrderMap {
private:
    struct Slot {
        uint64_t key;       // order_id (0 = empty, ~0 = tombstone)
        uint32_t value;     // index into order pool
        uint32_t padding_;
    };

    static constexpr size_t CAPACITY = MAX_ORDERS * 2;  // 50% load factor
    static constexpr size_t MASK = CAPACITY - 1;
    static_assert((CAPACITY & (CAPACITY - 1)) == 0, "must be power of 2");

    std::vector<Slot> slots_;

public:
    OrderMap() : slots_(CAPACITY, Slot{0, 0, 0}) {}

    void insert(uint64_t key, uint32_t value) {
        size_t idx = hash(key) & MASK;
        size_t first_tombstone = SIZE_MAX;

        while (slots_[idx].key != 0) {
            if (slots_[idx].key == TOMBSTONE) {
                if (first_tombstone == SIZE_MAX)
                    first_tombstone = idx;
            } else if (slots_[idx].key == key) {
                // Key already exists — update in place
                slots_[idx].value = value;
                return;
            }
            idx = (idx + 1) & MASK;
        }

        // Insert at first tombstone if we found one, otherwise at the empty slot
        size_t target = (first_tombstone != SIZE_MAX) ? first_tombstone : idx;
        slots_[target] = {key, value, 0};
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

    // Implemented as a stack — push on cancel, pop on add.
    // No heap allocation: uses a pre-allocated array.
    uint32_t freelist_[MAX_ORDERS];
    uint32_t freelist_top_ = 0;

    // Flat open-addressing map: order_id → pool index
    OrderMap order_map_;

    // --- Pool allocator with free list recycling ---
    uint32_t alloc_index() {
        if (freelist_top_ > 0)
            return freelist_[--freelist_top_];
        assert(pool_count_ < MAX_ORDERS && "Order pool exhausted");
        return pool_count_++;
    }

    void free_index(uint32_t idx) {
        freelist_[freelist_top_++] = idx;
    }

    void assert_price_in_range(PriceTick price) const {
        assert(price >= price_min_
            && price < price_min_ + static_cast<PriceTick>(num_levels_)
            && "Price out of configured tick range");
    }

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
        assert_price_in_range(price);
        return volume_levels_[price - price_min_];
    }

    // --- Hot path: add order — no heap allocation ---
    void add_order(uint64_t order_id, PriceTick price, int32_t quantity) {
        assert_price_in_range(price);
        uint32_t idx = alloc_index();
        order_pool_[idx] = {order_id, price, quantity, 0};
        order_map_.insert(order_id, idx);
        volume_levels_[price - price_min_] += quantity;
    }

    // --- Hot path: cancel order — recycles pool index via free list ---
    void cancel_order(uint64_t order_id) {
        uint32_t* idx_ptr = order_map_.find(order_id);
        if (!idx_ptr) return;

        uint32_t idx = *idx_ptr;
        Order& order = order_pool_[idx];
        volume_levels_[order.price - price_min_] -= order.quantity;
        order.quantity = 0;
        order_map_.erase(order_id);
        free_index(idx);
    }

    // --- Hot path: modify order — no allocation ---
    void modify_order(uint64_t order_id, PriceTick new_price, int32_t new_quantity) {
        uint32_t* idx_ptr = order_map_.find(order_id);
        if (!idx_ptr) return;

        assert_price_in_range(new_price);
        Order& order = order_pool_[*idx_ptr];

        volume_levels_[order.price - price_min_] -= order.quantity;

        order.price = new_price;
        order.quantity = new_quantity;
        volume_levels_[new_price - price_min_] += new_quantity;
    }

    // Live order count (total allocated minus recycled)
    size_t num_orders() const { return pool_count_ - freelist_top_; }
};
