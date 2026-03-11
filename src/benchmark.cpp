#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

#include "lom/order_book.hpp"

using namespace lom;

namespace {

uint64_t now_ns() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

} // namespace

int main() {
    constexpr std::size_t k_commands = 300000;

    LimitOrderBook::Config cfg{};
    cfg.max_order_nodes = 1'500'000;
    cfg.snapshot_depth = 20;
    LimitOrderBook book(cfg);

    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int64_t> price_dist(9900, 10100);
    std::uniform_int_distribution<uint64_t> qty_dist(1, 50);
    std::bernoulli_distribution side_dist(0.5);

    std::vector<uint64_t> latencies_ns;
    latencies_ns.reserve(k_commands);
    std::vector<Trade> trades;
    trades.reserve(256);

    uint64_t order_id = 1;
    for (std::size_t i = 0; i < k_commands; ++i) {
        trades.clear();
        NewOrder ord{};
        ord.order_id = order_id++;
        ord.user_id = 1000 + (i % 500);
        ord.side = side_dist(rng) ? Side::buy : Side::sell;
        ord.price_ticks = price_dist(rng);
        ord.quantity = qty_dist(rng);
        ord.client_ts_ns = now_ns();

        const auto t0 = std::chrono::steady_clock::now();
        (void)book.on_new_order(ord, trades, now_ns());
        const auto t1 = std::chrono::steady_clock::now();

        latencies_ns.push_back(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    }

    std::sort(latencies_ns.begin(), latencies_ns.end());

    auto pct = [&](double p) -> uint64_t {
        if (latencies_ns.empty()) return 0;
        const std::size_t idx = static_cast<std::size_t>(p * static_cast<double>(latencies_ns.size() - 1));
        return latencies_ns[idx];
    };

    const double p50_us = pct(0.50) / 1000.0;
    const double p95_us = pct(0.95) / 1000.0;
    const double p99_us = pct(0.99) / 1000.0;
    const double max_us = latencies_ns.back() / 1000.0;

    std::cout << "Benchmark complete\n";
    std::cout << "Commands: " << k_commands << '\n';
    std::cout << "Active resting orders: " << book.active_order_count() << '\n';
    std::cout << "Latency p50(us): " << p50_us << '\n';
    std::cout << "Latency p95(us): " << p95_us << '\n';
    std::cout << "Latency p99(us): " << p99_us << '\n';
    std::cout << "Latency max(us): " << max_us << '\n';
    return 0;
}
