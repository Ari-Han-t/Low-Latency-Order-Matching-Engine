#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "lom/lockfree_mpsc_queue.hpp"
#include "lom/order_book.hpp"
#include "lom/types.hpp"

namespace lom {

class MatchingEngine {
public:
    struct Config {
        std::size_t max_order_nodes = 1'000'000;
        std::size_t l2_depth = 20;
        std::size_t idle_sleep_us = 25;
    };

    using MarketDataCallback = std::function<void(const std::string&)>;

    MatchingEngine();
    explicit MatchingEngine(Config config);
    ~MatchingEngine();

    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    void set_market_data_callback(MarketDataCallback cb);

    bool submit(const Command& cmd);
    bool submit(Command&& cmd);

    void start();
    void stop();

    [[nodiscard]] bool running() const;
    [[nodiscard]] uint64_t sequence() const;
    [[nodiscard]] std::size_t active_order_count() const;

private:
    struct MarketEvent {
        std::string payload;
    };

    void matcher_loop();
    void publisher_loop();
    void publish_book_and_trades(const std::vector<Trade>& trades);
    static uint64_t now_ns();

    static constexpr std::size_t k_command_capacity = 65'536;
    static constexpr std::size_t k_market_capacity = 65'536;

    Config config_;
    LimitOrderBook book_;

    LockFreeMPSCQueue<Command, k_command_capacity> command_queue_;
    LockFreeMPSCQueue<MarketEvent, k_market_capacity> market_queue_;

    std::atomic<bool> running_{false};
    std::atomic<uint64_t> sequence_{0};

    MarketDataCallback market_data_callback_{};
    std::thread matcher_thread_;
    std::thread publisher_thread_;
};

} // namespace lom
