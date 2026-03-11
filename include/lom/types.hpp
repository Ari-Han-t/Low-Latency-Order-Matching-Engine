#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lom {

enum class Side : uint8_t {
    buy = 0,
    sell = 1,
};

enum class CommandType : uint8_t {
    new_order = 0,
    cancel_order = 1,
};

struct NewOrder {
    uint64_t order_id = 0;
    uint64_t user_id = 0;
    Side side = Side::buy;
    int64_t price_ticks = 0;
    uint64_t quantity = 0;
    uint64_t client_ts_ns = 0;
};

struct CancelOrder {
    uint64_t order_id = 0;
    uint64_t user_id = 0;
    uint64_t client_ts_ns = 0;
};

struct Command {
    CommandType type = CommandType::new_order;
    NewOrder new_order;
    CancelOrder cancel_order;
    uint64_t ingress_ts_ns = 0;
};

struct Trade {
    uint64_t taker_order_id = 0;
    uint64_t maker_order_id = 0;
    uint64_t buy_user_id = 0;
    uint64_t sell_user_id = 0;
    int64_t price_ticks = 0;
    uint64_t quantity = 0;
    uint64_t match_ts_ns = 0;
};

struct PriceLevelView {
    int64_t price_ticks = 0;
    uint64_t total_quantity = 0;
    uint64_t order_count = 0;
};

struct L2Snapshot {
    uint64_t sequence = 0;
    uint64_t ts_ns = 0;
    std::vector<PriceLevelView> bids;
    std::vector<PriceLevelView> asks;
};

inline const char* side_to_cstr(Side side) {
    return side == Side::buy ? "buy" : "sell";
}

inline std::string to_string(Side side) {
    return std::string(side_to_cstr(side));
}

} // namespace lom

