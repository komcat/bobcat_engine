// SocketGuard.h - RAII wrapper for Winsock SOCKET handles
#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define closesocket close
#endif

/**
 * @brief RAII wrapper for raw SOCKET handles
 *
 * Ensures sockets are properly closed when going out of scope or
 * when the owning object is destroyed. Move-only (non-copyable).
 *
 * Usage:
 *   SocketGuard sock(socket(AF_INET, SOCK_STREAM, 0));
 *   if (!sock.valid()) { ... }
 *   connect(sock.get(), ...);
 *   send(sock.get(), ...);
 *   sock.reset();  // explicit close
 */
class SocketGuard {
public:
    SocketGuard() : m_socket(INVALID_SOCKET) {}
    explicit SocketGuard(SOCKET s) : m_socket(s) {}

    ~SocketGuard() { close(); }

    // Non-copyable
    SocketGuard(const SocketGuard&) = delete;
    SocketGuard& operator=(const SocketGuard&) = delete;

    // Movable
    SocketGuard(SocketGuard&& other) noexcept : m_socket(other.m_socket) {
        other.m_socket = INVALID_SOCKET;
    }

    SocketGuard& operator=(SocketGuard&& other) noexcept {
        if (this != &other) {
            close();
            m_socket = other.m_socket;
            other.m_socket = INVALID_SOCKET;
        }
        return *this;
    }

    [[nodiscard]] SOCKET get() const { return m_socket; }
    [[nodiscard]] bool valid() const { return m_socket != INVALID_SOCKET; }

    SOCKET release() {
        SOCKET s = m_socket;
        m_socket = INVALID_SOCKET;
        return s;
    }

    void reset(SOCKET s = INVALID_SOCKET) {
        close();
        m_socket = s;
    }

private:
    void close() {
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
    }

    SOCKET m_socket;
};
