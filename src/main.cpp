#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "lom/matching_engine.hpp"
#include "lom/types.hpp"
#include "lom/websocket_server.hpp"

namespace {

using json = nlohmann::json;

std::atomic<bool> g_keep_running{true};
constexpr int k_schema_version = 1;

struct GatewayRiskLimits {
    int64_t min_price_ticks = 1;
    int64_t max_price_ticks = 2'000'000;
    uint64_t max_order_quantity = 250'000;
};

uint64_t now_ns() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

void signal_handler(int) {
    g_keep_running.store(false, std::memory_order_release);
}

bool parse_side(const std::string& value, lom::Side& out) {
    if (value == "buy" || value == "BUY" || value == "bid") {
        out = lom::Side::buy;
        return true;
    }
    if (value == "sell" || value == "SELL" || value == "ask") {
        out = lom::Side::sell;
        return true;
    }
    return false;
}

json ok_response(const std::string& type, const std::string& request_id, uint64_t ts_ns) {
    json out{
        {"schema_version", k_schema_version},
        {"type", "ack"},
        {"event", type},
        {"status", "accepted"},
        {"ts_ns", ts_ns},
    };
    if (!request_id.empty()) {
        out["request_id"] = request_id;
    }
    return out;
}

json error_response(
    const std::string& reason_code,
    const std::string& reason,
    const std::string& request_id,
    uint64_t ts_ns
) {
    json out = {
        {"schema_version", k_schema_version},
        {"type", "ack"},
        {"status", "rejected"},
        {"reason_code", reason_code},
        {"reason", reason},
        {"ts_ns", ts_ns},
    };
    if (!request_id.empty()) {
        out["request_id"] = request_id;
    }
    return out;
}

std::string parse_request_id(const json& inbound) {
    if (!inbound.contains("request_id")) {
        return {};
    }

    const auto& req = inbound["request_id"];
    if (req.is_string()) {
        return req.get<std::string>();
    }
    if (req.is_number_unsigned()) {
        return std::to_string(req.get<uint64_t>());
    }
    if (req.is_number_integer()) {
        return std::to_string(req.get<int64_t>());
    }
    return {};
}

bool validate_new_order(const lom::NewOrder& order, const GatewayRiskLimits& limits, std::string& reason) {
    if (order.order_id == 0) {
        reason = "order_id must be > 0";
        return false;
    }
    if (order.user_id == 0) {
        reason = "user_id must be > 0";
        return false;
    }
    if (order.quantity == 0) {
        reason = "quantity must be > 0";
        return false;
    }
    if (order.quantity > limits.max_order_quantity) {
        reason = "quantity exceeds max_order_quantity";
        return false;
    }
    if (order.price_ticks < limits.min_price_ticks || order.price_ticks > limits.max_price_ticks) {
        reason = "price_ticks outside configured risk band";
        return false;
    }
    return true;
}

bool validate_cancel_order(const lom::CancelOrder& order, std::string& reason) {
    if (order.order_id == 0) {
        reason = "order_id must be > 0";
        return false;
    }
    return true;
}

} // namespace

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    lom::MatchingEngine::Config engine_config{};
    engine_config.max_order_nodes = 1'000'000;
    engine_config.l2_depth = 20;
    engine_config.idle_sleep_us = 20;
    lom::MatchingEngine engine(engine_config);

    lom::WebSocketServer server;
    const GatewayRiskLimits risk_limits{};

    engine.set_market_data_callback([&server](const std::string& payload) {
        (void)server.broadcast(payload);
    });

    server.set_message_handler([&](uint64_t client_id, const std::string& payload) {
        try {
            const auto inbound = json::parse(payload);
            const std::string type = inbound.value("type", "");
            const std::string request_id = parse_request_id(inbound);

            lom::Command cmd{};
            cmd.ingress_ts_ns = now_ns();

            if (type == "new_order") {
                const auto side_value = inbound.value("side", "");
                lom::Side side{};
                if (!parse_side(side_value, side)) {
                    (void)server.send_to(
                        client_id,
                        error_response("invalid_side", "invalid side", request_id, now_ns()).dump()
                    );
                    return;
                }

                cmd.type = lom::CommandType::new_order;
                lom::NewOrder no{};
                no.order_id = inbound.at("order_id").get<uint64_t>();
                no.user_id = inbound.at("user_id").get<uint64_t>();
                no.side = side;
                no.price_ticks = inbound.at("price_ticks").get<int64_t>();
                no.quantity = inbound.at("quantity").get<uint64_t>();
                no.client_ts_ns = inbound.value("client_ts_ns", 0ull);
                std::string reject_reason;
                if (!validate_new_order(no, risk_limits, reject_reason)) {
                    (void)server.send_to(
                        client_id,
                        error_response("risk_reject", reject_reason, request_id, now_ns()).dump()
                    );
                    return;
                }
                cmd.new_order = no;
            } else if (type == "cancel_order") {
                cmd.type = lom::CommandType::cancel_order;
                lom::CancelOrder co{};
                co.order_id = inbound.at("order_id").get<uint64_t>();
                co.user_id = inbound.value("user_id", 0ull);
                co.client_ts_ns = inbound.value("client_ts_ns", 0ull);
                std::string reject_reason;
                if (!validate_cancel_order(co, reject_reason)) {
                    (void)server.send_to(
                        client_id,
                        error_response("invalid_cancel", reject_reason, request_id, now_ns()).dump()
                    );
                    return;
                }
                cmd.cancel_order = co;
            } else {
                (void)server.send_to(
                    client_id,
                    error_response("unsupported_type", "unsupported type", request_id, now_ns()).dump()
                );
                return;
            }

            if (!engine.submit(std::move(cmd))) {
                (void)server.send_to(
                    client_id,
                    error_response("ingress_queue_full", "ingress queue full", request_id, now_ns()).dump()
                );
                return;
            }

            (void)server.send_to(client_id, ok_response(type, request_id, now_ns()).dump());
        } catch (const std::exception& ex) {
            (void)server.send_to(
                client_id,
                error_response("bad_request", std::string("bad request: ") + ex.what(), "", now_ns()).dump()
            );
        }
    });

    engine.start();
    if (!server.start("0.0.0.0", 9002)) {
        std::cerr << "Failed to start WebSocket server on port 9002.\n";
        engine.stop();
        return 1;
    }

    std::cout << "Low-Latency Order Matching Engine running.\n";
    std::cout << "WebSocket API: ws://127.0.0.1:9002\n";
    std::cout << "Press Ctrl+C to stop.\n";

    while (g_keep_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "Shutting down...\n";
    server.stop();
    engine.stop();
    std::cout << "Final sequence: " << engine.sequence() << ", active orders: " << engine.active_order_count() << '\n';
    const auto stats = engine.stats();
    std::cout << "Engine stats: submit_ok=" << stats.submit_ok
              << ", submit_rejected=" << stats.submit_rejected
              << ", processed=" << stats.commands_processed
              << ", accepted=" << stats.commands_accepted
              << ", rejected=" << stats.commands_rejected
              << ", trades=" << stats.trades_emitted
              << ", matched_qty=" << stats.matched_quantity
              << ", market_events=" << stats.market_events_emitted << '\n';
    return 0;
}
