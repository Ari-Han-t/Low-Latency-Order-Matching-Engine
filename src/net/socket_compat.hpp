#pragma once

#include <cstdint>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_handle_t = SOCKET;
constexpr socket_handle_t k_invalid_socket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using socket_handle_t = int;
constexpr socket_handle_t k_invalid_socket = -1;
#endif

namespace lom::net {

inline bool init_network_stack() {
#ifdef _WIN32
    WSADATA wsa_data{};
    return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
#else
    return true;
#endif
}

inline void cleanup_network_stack() {
#ifdef _WIN32
    WSACleanup();
#endif
}

inline void close_socket(socket_handle_t sock) {
    if (sock == k_invalid_socket) {
        return;
    }
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

inline int last_socket_error() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

} // namespace lom::net
