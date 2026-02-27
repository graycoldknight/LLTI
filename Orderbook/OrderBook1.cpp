struct Order {
    bool is_active;
    double price;
    uint64_t order_id;
    int32_t quantity;
    std::string symbol;
};

class OrderBook {
private:
    std::vector<Order*> orders;

public:
    int32_t get_volume_at_price(double target_price) {
        int32_t total_volume = 0;
        for (Order* order : orders) {
            if (order->is_active && order->price == target_price) {
                total_volume += order->quantity;
            }
        }
        return total_volume;
    }
};