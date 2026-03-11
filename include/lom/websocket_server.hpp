#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace lom {

class WebSocketServer {
public:
    using MessageHandler = std::function<void(uint64_t client_id, const std::string& payload)>;

    WebSocketServer();
    ~WebSocketServer();

    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    void set_message_handler(MessageHandler handler);

    bool start(const std::string& bind_address, uint16_t port);
    void stop();

    bool broadcast(const std::string& payload);
    bool send_to(uint64_t client_id, const std::string& payload);

    [[nodiscard]] bool running() const;
    [[nodiscard]] std::size_t client_count() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lom

