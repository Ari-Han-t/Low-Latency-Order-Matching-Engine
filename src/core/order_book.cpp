#include "lom/order_book.hpp"

#include <algorithm>
#include <utility>

namespace lom {

LimitOrderBook::LimitOrderBook()
    : LimitOrderBook(Config{}) {}

LimitOrderBook::LimitOrderBook(Config config)
    : config_(config),
      pool_(config.max_order_nodes) {}

bool LimitOrderBook::on_new_order(const NewOrder& order, std::vector<Trade>& trades_out, uint64_t match_ts_ns) {
    if (order.quantity == 0 || order.price_ticks <= 0) {
        return false;
    }
    if (order_index_.find(order.order_id) != order_index_.end()) {
        return false;
    }

    if (order.side == Side::buy) {
        return match_buy(order, trades_out, match_ts_ns);
    }
    return match_sell(order, trades_out, match_ts_ns);
}

bool LimitOrderBook::on_cancel_order(const CancelOrder& order) {
    auto it = order_index_.find(order.order_id);
    if (it == order_index_.end()) {
        return false;
    }
    OrderNode* node = it->second;
    if (order.user_id != 0 && node->user_id != order.user_id) {
        return false;
    }
    remove_order_node(node);
    return true;
}

bool LimitOrderBook::match_buy(NewOrder taker, std::vector<Trade>& trades_out, uint64_t ts_ns) {
    auto unlink_filled = [&](PriceLevel& level, OrderNode* maker) {
        if (maker->prev) {
            maker->prev->next = maker->next;
        } else {
            level.head = maker->next;
        }
        if (maker->next) {
            maker->next->prev = maker->prev;
        } else {
            level.tail = maker->prev;
        }

        level.order_count--;
        order_index_.erase(maker->order_id);
        maker->next = nullptr;
        maker->prev = nullptr;
        maker->open_qty = 0;
        pool_.release(maker);
    };

    while (taker.quantity > 0 && !asks_.empty()) {
        auto ask_it = asks_.begin();
        if (ask_it->first > taker.price_ticks) {
            break;
        }

        PriceLevel& level = ask_it->second;
        OrderNode* maker = level.head;
        if (maker == nullptr) {
            asks_.erase(ask_it);
            continue;
        }

        const uint64_t trade_qty = std::min<uint64_t>(taker.quantity, maker->open_qty);
        taker.quantity -= trade_qty;
        maker->open_qty -= trade_qty;
        level.total_qty -= trade_qty;

        Trade trd{};
        trd.taker_order_id = taker.order_id;
        trd.maker_order_id = maker->order_id;
        trd.buy_user_id = taker.user_id;
        trd.sell_user_id = maker->user_id;
        trd.price_ticks = maker->price_ticks;
        trd.quantity = trade_qty;
        trd.match_ts_ns = ts_ns;
        trades_out.push_back(trd);

        if (maker->open_qty == 0) {
            unlink_filled(level, maker);
        }

        if (level.order_count == 0) {
            asks_.erase(ask_it);
        }
    }

    if (taker.quantity > 0) {
        return add_resting_order(taker) != nullptr;
    }
    return true;
}

bool LimitOrderBook::match_sell(NewOrder taker, std::vector<Trade>& trades_out, uint64_t ts_ns) {
    auto unlink_filled = [&](PriceLevel& level, OrderNode* maker) {
        if (maker->prev) {
            maker->prev->next = maker->next;
        } else {
            level.head = maker->next;
        }
        if (maker->next) {
            maker->next->prev = maker->prev;
        } else {
            level.tail = maker->prev;
        }

        level.order_count--;
        order_index_.erase(maker->order_id);
        maker->next = nullptr;
        maker->prev = nullptr;
        maker->open_qty = 0;
        pool_.release(maker);
    };

    while (taker.quantity > 0 && !bids_.empty()) {
        auto bid_it = bids_.begin();
        if (bid_it->first < taker.price_ticks) {
            break;
        }

        PriceLevel& level = bid_it->second;
        OrderNode* maker = level.head;
        if (maker == nullptr) {
            bids_.erase(bid_it);
            continue;
        }

        const uint64_t trade_qty = std::min<uint64_t>(taker.quantity, maker->open_qty);
        taker.quantity -= trade_qty;
        maker->open_qty -= trade_qty;
        level.total_qty -= trade_qty;

        Trade trd{};
        trd.taker_order_id = taker.order_id;
        trd.maker_order_id = maker->order_id;
        trd.buy_user_id = maker->user_id;
        trd.sell_user_id = taker.user_id;
        trd.price_ticks = maker->price_ticks;
        trd.quantity = trade_qty;
        trd.match_ts_ns = ts_ns;
        trades_out.push_back(trd);

        if (maker->open_qty == 0) {
            unlink_filled(level, maker);
        }

        if (level.order_count == 0) {
            bids_.erase(bid_it);
        }
    }

    if (taker.quantity > 0) {
        return add_resting_order(taker) != nullptr;
    }
    return true;
}

LimitOrderBook::OrderNode* LimitOrderBook::add_resting_order(const NewOrder& order) {
    OrderNode* node = pool_.acquire();
    if (node == nullptr) {
        return nullptr;
    }

    node->order_id = order.order_id;
    node->user_id = order.user_id;
    node->side = order.side;
    node->price_ticks = order.price_ticks;
    node->open_qty = order.quantity;
    node->next = nullptr;
    node->prev = nullptr;

    if (order.side == Side::buy) {
        auto inserted_pair = bids_.insert(std::make_pair(order.price_ticks, PriceLevel{}));
        auto it = inserted_pair.first;
        bool inserted = inserted_pair.second;
        PriceLevel& level = it->second;
        if (inserted) {
            level.price_ticks = order.price_ticks;
        }
        if (level.tail) {
            level.tail->next = node;
            node->prev = level.tail;
            level.tail = node;
        } else {
            level.head = node;
            level.tail = node;
        }
        level.total_qty += node->open_qty;
        level.order_count++;
    } else {
        auto inserted_pair = asks_.insert(std::make_pair(order.price_ticks, PriceLevel{}));
        auto it = inserted_pair.first;
        bool inserted = inserted_pair.second;
        PriceLevel& level = it->second;
        if (inserted) {
            level.price_ticks = order.price_ticks;
        }
        if (level.tail) {
            level.tail->next = node;
            node->prev = level.tail;
            level.tail = node;
        } else {
            level.head = node;
            level.tail = node;
        }
        level.total_qty += node->open_qty;
        level.order_count++;
    }

    order_index_[order.order_id] = node;
    return node;
}

void LimitOrderBook::remove_order_node(OrderNode* node) {
    if (node == nullptr) {
        return;
    }

    if (node->side == Side::buy) {
        auto it = bids_.find(node->price_ticks);
        if (it == bids_.end()) {
            return;
        }
        PriceLevel& level = it->second;
        if (node->prev) {
            node->prev->next = node->next;
        } else {
            level.head = node->next;
        }
        if (node->next) {
            node->next->prev = node->prev;
        } else {
            level.tail = node->prev;
        }
        level.total_qty -= node->open_qty;
        level.order_count--;
        if (level.order_count == 0) {
            bids_.erase(it);
        }
    } else {
        auto it = asks_.find(node->price_ticks);
        if (it == asks_.end()) {
            return;
        }
        PriceLevel& level = it->second;
        if (node->prev) {
            node->prev->next = node->next;
        } else {
            level.head = node->next;
        }
        if (node->next) {
            node->next->prev = node->prev;
        } else {
            level.tail = node->prev;
        }
        level.total_qty -= node->open_qty;
        level.order_count--;
        if (level.order_count == 0) {
            asks_.erase(it);
        }
    }

    order_index_.erase(node->order_id);
    node->next = nullptr;
    node->prev = nullptr;
    node->open_qty = 0;
    pool_.release(node);
}

bool LimitOrderBook::append_price_level_snapshot(
    const BidLevels& bids,
    const AskLevels& asks,
    L2Snapshot& out
) const {
    out.bids.clear();
    out.asks.clear();

    out.bids.reserve(config_.snapshot_depth);
    out.asks.reserve(config_.snapshot_depth);

    std::size_t count = 0;
    for (BidLevels::const_iterator it = bids.begin(); it != bids.end(); ++it) {
        if (count++ >= config_.snapshot_depth) {
            break;
        }
        const int64_t price = it->first;
        const PriceLevel& level = it->second;
        PriceLevelView view{};
        view.price_ticks = price;
        view.total_quantity = level.total_qty;
        view.order_count = level.order_count;
        out.bids.push_back(view);
    }

    count = 0;
    for (AskLevels::const_iterator it = asks.begin(); it != asks.end(); ++it) {
        if (count++ >= config_.snapshot_depth) {
            break;
        }
        const int64_t price = it->first;
        const PriceLevel& level = it->second;
        PriceLevelView view{};
        view.price_ticks = price;
        view.total_quantity = level.total_qty;
        view.order_count = level.order_count;
        out.asks.push_back(view);
    }

    return true;
}

L2Snapshot LimitOrderBook::snapshot(uint64_t sequence, uint64_t ts_ns) const {
    L2Snapshot out{};
    out.sequence = sequence;
    out.ts_ns = ts_ns;
    append_price_level_snapshot(bids_, asks_, out);
    return out;
}

std::size_t LimitOrderBook::active_order_count() const {
    return order_index_.size();
}

std::size_t LimitOrderBook::snapshot_depth() const {
    return config_.snapshot_depth;
}

} // namespace lom
