#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include "lom/order_pool.hpp"
#include "lom/types.hpp"

namespace lom {

class LimitOrderBook {
public:
    struct Config {
        std::size_t max_order_nodes = 1'000'000;
        std::size_t snapshot_depth = 20;
    };

    LimitOrderBook();
    explicit LimitOrderBook(Config config);

    bool on_new_order(const NewOrder& order, std::vector<Trade>& trades_out, uint64_t match_ts_ns);
    bool on_cancel_order(const CancelOrder& order);
    L2Snapshot snapshot(uint64_t sequence, uint64_t ts_ns) const;

    [[nodiscard]] std::size_t active_order_count() const;
    [[nodiscard]] std::size_t snapshot_depth() const;

private:
    struct OrderNode {
        uint64_t order_id = 0;
        uint64_t user_id = 0;
        Side side = Side::buy;
        int64_t price_ticks = 0;
        uint64_t open_qty = 0;
        OrderNode* next = nullptr;
        OrderNode* prev = nullptr;
    };

    struct PriceLevel {
        int64_t price_ticks = 0;
        uint64_t total_qty = 0;
        uint64_t order_count = 0;
        OrderNode* head = nullptr;
        OrderNode* tail = nullptr;
    };

    using BidLevels = std::map<int64_t, PriceLevel, std::greater<int64_t>>;
    using AskLevels = std::map<int64_t, PriceLevel, std::less<int64_t>>;

    OrderNode* add_resting_order(const NewOrder& order);
    void remove_order_node(OrderNode* node);

    bool match_buy(NewOrder taker, std::vector<Trade>& trades_out, uint64_t ts_ns);
    bool match_sell(NewOrder taker, std::vector<Trade>& trades_out, uint64_t ts_ns);
    bool append_price_level_snapshot(
        const BidLevels& bids,
        const AskLevels& asks,
        L2Snapshot& out
    ) const;

    Config config_;
    LockFreeObjectPool<OrderNode> pool_;
    BidLevels bids_;
    AskLevels asks_;
    std::unordered_map<uint64_t, OrderNode*> order_index_;
};

} // namespace lom
