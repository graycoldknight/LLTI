#include <cstdint>
#include <vector>
#include <unordered_map>

// Price stored as integer ticks to avoid floating-point comparison issues.
// Example: if tick_size = 0.01, price 150.25 -> tick 15025
using PriceTick = int64_t;

struct Order {
    uint64_t order_id;
    PriceTick price;
    int32_t quantity;
};

class OrderBook {
private:
    // Per-order storage (for cancel/modify by order_id)
    std::unordered_map<uint64_t, Order> orders_;

    // O(1) price-level index: price tick -> aggregate volume
    std::unordered_map<PriceTick, int32_t> volume_at_price_;

public:
    // O(1) amortized — update the price-level index incrementally
    void add_order(uint64_t order_id, PriceTick price, int32_t quantity) {
        orders_.insert({order_id, {order_id, price, quantity}});
        volume_at_price_[price] += quantity;
    }

    // O(1) amortized — reverse the volume contribution, then remove
    void cancel_order(uint64_t order_id) {
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return;

        const Order& order = it->second;
        auto vol_it = volume_at_price_.find(order.price);
        vol_it->second -= order.quantity;
        if (vol_it->second == 0) {
            volume_at_price_.erase(vol_it);
        }

        orders_.erase(it);
    }

    // O(1) — single hash lookup, one cache miss instead of scanning 80 MB
    int32_t get_volume_at_price(PriceTick price) const {
        auto it = volume_at_price_.find(price);
        return it != volume_at_price_.end() ? it->second : 0;
    }

    // O(1) amortized — adjust both old and new price levels
    void modify_order(uint64_t order_id, PriceTick new_price, int32_t new_quantity) {
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return;

        Order& order = it->second;

        // Decrement old price level
        auto old_vol = volume_at_price_.find(order.price);
        old_vol->second -= order.quantity;
        if (old_vol->second == 0) {
            volume_at_price_.erase(old_vol);
        }

        // Update order and increment new price level
        order.price = new_price;
        order.quantity = new_quantity;
        volume_at_price_[new_price] += new_quantity;
    }

    size_t num_orders() const { return orders_.size(); }
    size_t num_price_levels() const { return volume_at_price_.size(); }
};
