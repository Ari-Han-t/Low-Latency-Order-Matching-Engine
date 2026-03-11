#include "lom/matching_engine.hpp"

#include <chrono>
#include <thread>

#include <nlohmann/json.hpp>

namespace lom {

using json = nlohmann::json;

MatchingEngine::MatchingEngine()
    : MatchingEngine(Config{}) {}

MatchingEngine::MatchingEngine(Config config)
    : config_(config),
      book_([&config] {
          LimitOrderBook::Config c{};
          c.max_order_nodes = config.max_order_nodes;
          c.snapshot_depth = config.l2_depth;
          return c;
      }()) {}

MatchingEngine::~MatchingEngine() {
    stop();
}

void MatchingEngine::set_market_data_callback(MarketDataCallback cb) {
    market_data_callback_ = std::move(cb);
}

bool MatchingEngine::submit(const Command& cmd) {
    const bool ok = command_queue_.try_push(cmd);
    if (ok) {
        submit_ok_.fetch_add(1, std::memory_order_relaxed);
    } else {
        submit_rejected_.fetch_add(1, std::memory_order_relaxed);
    }
    return ok;
}

bool MatchingEngine::submit(Command&& cmd) {
    const bool ok = command_queue_.try_push(std::move(cmd));
    if (ok) {
        submit_ok_.fetch_add(1, std::memory_order_relaxed);
    } else {
        submit_rejected_.fetch_add(1, std::memory_order_relaxed);
    }
    return ok;
}

void MatchingEngine::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    matcher_thread_ = std::thread([this] { matcher_loop(); });
    publisher_thread_ = std::thread([this] { publisher_loop(); });
}

void MatchingEngine::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;
    }

    if (matcher_thread_.joinable()) {
        matcher_thread_.join();
    }
    if (publisher_thread_.joinable()) {
        publisher_thread_.join();
    }
}

bool MatchingEngine::running() const {
    return running_.load(std::memory_order_acquire);
}

uint64_t MatchingEngine::sequence() const {
    return sequence_.load(std::memory_order_acquire);
}

std::size_t MatchingEngine::active_order_count() const {
    return book_.active_order_count();
}

MatchingEngine::Stats MatchingEngine::stats() const {
    Stats out{};
    out.submit_ok = submit_ok_.load(std::memory_order_relaxed);
    out.submit_rejected = submit_rejected_.load(std::memory_order_relaxed);
    out.commands_processed = commands_processed_.load(std::memory_order_relaxed);
    out.commands_accepted = commands_accepted_.load(std::memory_order_relaxed);
    out.commands_rejected = commands_rejected_.load(std::memory_order_relaxed);
    out.trades_emitted = trades_emitted_.load(std::memory_order_relaxed);
    out.matched_quantity = matched_quantity_.load(std::memory_order_relaxed);
    out.market_events_emitted = market_events_emitted_.load(std::memory_order_relaxed);
    return out;
}

void MatchingEngine::matcher_loop() {
    while (running_.load(std::memory_order_acquire) || command_queue_.size_approx() > 0) {
        Command cmd{};
        if (!command_queue_.try_pop(cmd)) {
            std::this_thread::sleep_for(std::chrono::microseconds(config_.idle_sleep_us));
            continue;
        }
        commands_processed_.fetch_add(1, std::memory_order_relaxed);

        std::vector<Trade> trades;
        bool accepted = false;

        if (cmd.type == CommandType::new_order) {
            accepted = book_.on_new_order(cmd.new_order, trades, now_ns());
        } else if (cmd.type == CommandType::cancel_order) {
            accepted = book_.on_cancel_order(cmd.cancel_order);
        }

        if (accepted) {
            commands_accepted_.fetch_add(1, std::memory_order_relaxed);
            trades_emitted_.fetch_add(static_cast<uint64_t>(trades.size()), std::memory_order_relaxed);
            uint64_t traded_qty = 0;
            for (std::size_t i = 0; i < trades.size(); ++i) {
                traded_qty += trades[i].quantity;
            }
            matched_quantity_.fetch_add(traded_qty, std::memory_order_relaxed);
            publish_book_and_trades(trades);
        } else {
            commands_rejected_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void MatchingEngine::publisher_loop() {
    while (running_.load(std::memory_order_acquire) || market_queue_.size_approx() > 0) {
        MarketEvent evt{};
        if (!market_queue_.try_pop(evt)) {
            std::this_thread::sleep_for(std::chrono::microseconds(config_.idle_sleep_us));
            continue;
        }

        if (market_data_callback_) {
            market_data_callback_(evt.payload);
        }
    }
}

void MatchingEngine::publish_book_and_trades(const std::vector<Trade>& trades) {
    const uint64_t seq = sequence_.fetch_add(1, std::memory_order_acq_rel) + 1;
    const uint64_t ts = now_ns();
    L2Snapshot snap = book_.snapshot(seq, ts);

    json payload = {
        {"schema_version", 1},
        {"type", "l2"},
        {"sequence", snap.sequence},
        {"ts_ns", snap.ts_ns},
        {"bids", json::array()},
        {"asks", json::array()},
        {"trades", json::array()},
    };

    for (const auto& lvl : snap.bids) {
        payload["bids"].push_back({lvl.price_ticks, lvl.total_quantity, lvl.order_count});
    }
    for (const auto& lvl : snap.asks) {
        payload["asks"].push_back({lvl.price_ticks, lvl.total_quantity, lvl.order_count});
    }
    for (const auto& trd : trades) {
        payload["trades"].push_back({
            {"taker_order_id", trd.taker_order_id},
            {"maker_order_id", trd.maker_order_id},
            {"buy_user_id", trd.buy_user_id},
            {"sell_user_id", trd.sell_user_id},
            {"price_ticks", trd.price_ticks},
            {"quantity", trd.quantity},
            {"match_ts_ns", trd.match_ts_ns},
        });
    }

    MarketEvent evt{};
    evt.payload = payload.dump();
    while (!market_queue_.try_push(std::move(evt))) {
        std::this_thread::sleep_for(std::chrono::microseconds(config_.idle_sleep_us));
    }
    market_events_emitted_.fetch_add(1, std::memory_order_relaxed);
}

uint64_t MatchingEngine::now_ns() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

} // namespace lom
