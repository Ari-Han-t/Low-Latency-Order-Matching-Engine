#include "lom/websocket_server.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "socket_compat.hpp"
#include "base64.hpp"
#include "sha1.hpp"

namespace lom {

namespace {

constexpr const char* k_ws_magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

struct WsFrame {
    uint8_t opcode = 0;
    std::string payload;
};

std::string trim(std::string value) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char c) { return !is_space(static_cast<unsigned char>(c)); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char c) { return !is_space(static_cast<unsigned char>(c)); }).base(), value.end());
    return value;
}

bool recv_exact(socket_handle_t sock, uint8_t* out, std::size_t len) {
    std::size_t received = 0;
    while (received < len) {
        const int n = recv(sock, reinterpret_cast<char*>(out + received), static_cast<int>(len - received), 0);
        if (n <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(n);
    }
    return true;
}

bool send_all(socket_handle_t sock, const uint8_t* data, std::size_t len) {
    std::size_t sent = 0;
    while (sent < len) {
        const int n = send(sock, reinterpret_cast<const char*>(data + sent), static_cast<int>(len - sent), 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool send_frame(socket_handle_t sock, uint8_t opcode, const std::string& payload) {
    std::vector<uint8_t> frame;
    frame.reserve(2 + payload.size() + 10);
    frame.push_back(static_cast<uint8_t>(0x80u | (opcode & 0x0Fu)));

    const uint64_t len = static_cast<uint64_t>(payload.size());
    if (len <= 125) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 0xFFFFu) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFFu));
        frame.push_back(static_cast<uint8_t>(len & 0xFFu));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFFu));
        }
    }

    frame.insert(frame.end(), payload.begin(), payload.end());
    return send_all(sock, frame.data(), frame.size());
}

bool read_frame(socket_handle_t sock, WsFrame& out) {
    uint8_t h[2]{};
    if (!recv_exact(sock, h, 2)) {
        return false;
    }

    const bool fin = (h[0] & 0x80u) != 0;
    const uint8_t opcode = static_cast<uint8_t>(h[0] & 0x0Fu);
    const bool masked = (h[1] & 0x80u) != 0;
    uint64_t payload_len = static_cast<uint64_t>(h[1] & 0x7Fu);

    if (!fin) {
        return false;
    }
    if (!masked) {
        return false;
    }

    if (payload_len == 126) {
        uint8_t ext[2]{};
        if (!recv_exact(sock, ext, 2)) {
            return false;
        }
        payload_len = (static_cast<uint64_t>(ext[0]) << 8) | static_cast<uint64_t>(ext[1]);
    } else if (payload_len == 127) {
        uint8_t ext[8]{};
        if (!recv_exact(sock, ext, 8)) {
            return false;
        }
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | static_cast<uint64_t>(ext[i]);
        }
    }

    if (payload_len > 8ull * 1024ull * 1024ull) {
        return false;
    }

    uint8_t mask[4]{};
    if (!recv_exact(sock, mask, 4)) {
        return false;
    }

    std::string payload;
    payload.resize(static_cast<std::size_t>(payload_len));
    if (payload_len > 0) {
        if (!recv_exact(sock, reinterpret_cast<uint8_t*>(&payload[0]), static_cast<std::size_t>(payload_len))) {
            return false;
        }
        for (std::size_t i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<char>(static_cast<uint8_t>(payload[i]) ^ mask[i % 4]);
        }
    }

    out.opcode = opcode;
    out.payload = std::move(payload);
    return true;
}

bool read_http_request(socket_handle_t sock, std::string& request) {
    request.clear();
    request.reserve(4096);

    char buffer[1024];
    while (request.find("\r\n\r\n") == std::string::npos) {
        const int n = recv(sock, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            return false;
        }
        request.append(buffer, buffer + n);
        if (request.size() > 65536) {
            return false;
        }
    }
    return true;
}

std::string find_header_value(const std::string& request, const std::string& header_name) {
    std::istringstream stream(request);
    std::string line;
    const std::string target = header_name + ":";

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() < target.size()) {
            continue;
        }
        std::string maybe = line.substr(0, target.size());
        std::transform(maybe.begin(), maybe.end(), maybe.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::string target_lower = target;
        std::transform(target_lower.begin(), target_lower.end(), target_lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (maybe == target_lower) {
            return trim(line.substr(target.size()));
        }
    }
    return {};
}

std::string websocket_accept_key(const std::string& ws_key) {
    const std::string material = ws_key + k_ws_magic;
    const auto digest = util::sha1(material);
    return util::base64_encode(digest.data(), digest.size());
}

bool send_handshake_response(socket_handle_t sock, const std::string& accept_key) {
    std::ostringstream ss;
    ss << "HTTP/1.1 101 Switching Protocols\r\n";
    ss << "Upgrade: websocket\r\n";
    ss << "Connection: Upgrade\r\n";
    ss << "Sec-WebSocket-Accept: " << accept_key << "\r\n\r\n";
    const std::string response = ss.str();
    return send_all(sock, reinterpret_cast<const uint8_t*>(response.data()), response.size());
}

bool perform_handshake(socket_handle_t sock) {
    std::string request;
    if (!read_http_request(sock, request)) {
        return false;
    }

    if (request.rfind("GET ", 0) != 0) {
        return false;
    }

    const std::string key = find_header_value(request, "Sec-WebSocket-Key");
    if (key.empty()) {
        return false;
    }

    const std::string accept = websocket_accept_key(key);
    return send_handshake_response(sock, accept);
}

} // namespace

class WebSocketServer::Impl {
public:
    struct ClientSession {
        uint64_t id = 0;
        socket_handle_t socket = k_invalid_socket;
        std::atomic<bool> alive{true};
        std::mutex send_mu;
    };

    ~Impl() {
        stop();
    }

    void set_message_handler(MessageHandler handler) {
        std::lock_guard<std::mutex> lock(handler_mu_);
        message_handler_ = std::move(handler);
    }

    bool start(const std::string& bind_address, uint16_t port) {
        if (running_.load(std::memory_order_acquire)) {
            return false;
        }
        if (!net::init_network_stack()) {
            return false;
        }

        listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_socket_ == k_invalid_socket) {
            net::cleanup_network_stack();
            return false;
        }

        int opt = 1;
        setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        if (bind_address.empty() || bind_address == "0.0.0.0") {
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
        } else {
            if (inet_pton(AF_INET, bind_address.c_str(), &addr.sin_addr) <= 0) {
                net::close_socket(listen_socket_);
                listen_socket_ = k_invalid_socket;
                net::cleanup_network_stack();
                return false;
            }
        }

        if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            net::close_socket(listen_socket_);
            listen_socket_ = k_invalid_socket;
            net::cleanup_network_stack();
            return false;
        }

        if (listen(listen_socket_, SOMAXCONN) != 0) {
            net::close_socket(listen_socket_);
            listen_socket_ = k_invalid_socket;
            net::cleanup_network_stack();
            return false;
        }

        running_.store(true, std::memory_order_release);
        accept_thread_ = std::thread([this] { accept_loop(); });
        return true;
    }

    void stop() {
        if (!running_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        if (listen_socket_ != k_invalid_socket) {
#ifdef _WIN32
            shutdown(listen_socket_, SD_BOTH);
#else
            shutdown(listen_socket_, SHUT_RDWR);
#endif
            net::close_socket(listen_socket_);
            listen_socket_ = k_invalid_socket;
        }

        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }

        std::vector<std::shared_ptr<ClientSession>> snapshot;
        {
            std::lock_guard<std::mutex> lock(clients_mu_);
            for (std::unordered_map<uint64_t, std::shared_ptr<ClientSession>>::iterator it = clients_.begin(); it != clients_.end(); ++it) {
                snapshot.push_back(it->second);
            }
            clients_.clear();
        }

        for (auto& client : snapshot) {
            client->alive.store(false, std::memory_order_release);
#ifdef _WIN32
            shutdown(client->socket, SD_BOTH);
#else
            shutdown(client->socket, SHUT_RDWR);
#endif
            net::close_socket(client->socket);
            client->socket = k_invalid_socket;
        }

        net::cleanup_network_stack();
    }

    bool broadcast(const std::string& payload) {
        std::vector<std::shared_ptr<ClientSession>> snapshot;
        {
            std::lock_guard<std::mutex> lock(clients_mu_);
            snapshot.reserve(clients_.size());
            for (std::unordered_map<uint64_t, std::shared_ptr<ClientSession>>::iterator it = clients_.begin(); it != clients_.end(); ++it) {
                snapshot.push_back(it->second);
            }
        }

        bool any_sent = false;
        for (auto& client : snapshot) {
            any_sent |= send_to_client(client, payload);
        }
        return any_sent;
    }

    bool send_to(uint64_t client_id, const std::string& payload) {
        std::shared_ptr<ClientSession> target;
        {
            std::lock_guard<std::mutex> lock(clients_mu_);
            auto it = clients_.find(client_id);
            if (it == clients_.end()) {
                return false;
            }
            target = it->second;
        }
        return send_to_client(target, payload);
    }

    bool running() const {
        return running_.load(std::memory_order_acquire);
    }

    std::size_t client_count() const {
        std::lock_guard<std::mutex> lock(clients_mu_);
        return clients_.size();
    }

private:
    void accept_loop() {
        while (running_.load(std::memory_order_acquire)) {
            sockaddr_in client_addr{};
#ifdef _WIN32
            int client_len = sizeof(client_addr);
#else
            socklen_t client_len = sizeof(client_addr);
#endif
            socket_handle_t client_sock = accept(listen_socket_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_sock == k_invalid_socket) {
                if (!running_.load(std::memory_order_acquire)) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            auto client = std::make_shared<ClientSession>();
            client->id = next_client_id_.fetch_add(1, std::memory_order_relaxed);
            client->socket = client_sock;

            {
                std::lock_guard<std::mutex> lock(clients_mu_);
                clients_[client->id] = client;
            }

            std::thread([this, client] { client_loop(client); }).detach();
        }
    }

    void client_loop(const std::shared_ptr<ClientSession>& client) {
        if (!perform_handshake(client->socket)) {
            remove_client(client->id);
            net::close_socket(client->socket);
            client->socket = k_invalid_socket;
            return;
        }

        while (running_.load(std::memory_order_acquire) && client->alive.load(std::memory_order_acquire)) {
            WsFrame frame{};
            if (!read_frame(client->socket, frame)) {
                break;
            }
            if (frame.opcode == 0x8) {
                (void)send_frame(client->socket, 0x8, "");
                break;
            }
            if (frame.opcode == 0x9) {
                (void)send_frame(client->socket, 0xA, frame.payload);
                continue;
            }
            if (frame.opcode != 0x1) {
                continue;
            }

            MessageHandler cb;
            {
                std::lock_guard<std::mutex> lock(handler_mu_);
                cb = message_handler_;
            }
            if (cb) {
                cb(client->id, frame.payload);
            }
        }

        remove_client(client->id);
        net::close_socket(client->socket);
        client->socket = k_invalid_socket;
    }

    bool send_to_client(const std::shared_ptr<ClientSession>& client, const std::string& payload) {
        if (!client || client->socket == k_invalid_socket) {
            return false;
        }
        std::lock_guard<std::mutex> lock(client->send_mu);
        if (!client->alive.load(std::memory_order_acquire)) {
            return false;
        }
        if (!send_frame(client->socket, 0x1, payload)) {
            client->alive.store(false, std::memory_order_release);
            return false;
        }
        return true;
    }

    void remove_client(uint64_t client_id) {
        std::lock_guard<std::mutex> lock(clients_mu_);
        clients_.erase(client_id);
    }

    mutable std::mutex clients_mu_;
    std::unordered_map<uint64_t, std::shared_ptr<ClientSession>> clients_;
    std::atomic<uint64_t> next_client_id_{1};

    std::atomic<bool> running_{false};
    socket_handle_t listen_socket_{k_invalid_socket};
    std::thread accept_thread_;

    mutable std::mutex handler_mu_;
    MessageHandler message_handler_{};
};

WebSocketServer::WebSocketServer() : impl_(std::make_unique<Impl>()) {}

WebSocketServer::~WebSocketServer() = default;

void WebSocketServer::set_message_handler(MessageHandler handler) {
    impl_->set_message_handler(std::move(handler));
}

bool WebSocketServer::start(const std::string& bind_address, uint16_t port) {
    return impl_->start(bind_address, port);
}

void WebSocketServer::stop() {
    impl_->stop();
}

bool WebSocketServer::broadcast(const std::string& payload) {
    return impl_->broadcast(payload);
}

bool WebSocketServer::send_to(uint64_t client_id, const std::string& payload) {
    return impl_->send_to(client_id, payload);
}

bool WebSocketServer::running() const {
    return impl_->running();
}

std::size_t WebSocketServer::client_count() const {
    return impl_->client_count();
}

} // namespace lom
