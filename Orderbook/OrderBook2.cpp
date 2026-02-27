
class OrderBook {
private:
    std::vector<double> prices;
    std::vector<int32_t> quantities;
    std::vector<uint64_t> order_ids;
    std::vector<char[8]> symbols;
    
    std::vector<uint64_t> active_flags; 

public:
    int32_t get_volume_at_price(double target_price) {
        int32_t total_volume = 0;
        size_t num_orders = prices.size();
        
        for (size_t i = 0; i < num_orders; ++i) {
            if (is_active(i) && prices[i] == target_price) {
                total_volume += quantities[i];
            }
        }
        return total_volume;
    }
};