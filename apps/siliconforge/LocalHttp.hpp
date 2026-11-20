#pragma once

#include <cstring>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef small
#undef small
#endif
#pragma comment(lib, "ws2_32.lib")
#else
#include <netdb.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

/// One blocking HTTP/1.1 POST to the window on the loopback interface: what `siliconforge bench` uses to hand a run to
/// the window it was started from (#80). The server answers one request per connection and closes it, so the answer is
/// read to the end of the stream. No TLS, no redirect, no chunked body: the window's MCP server needs none of them.
namespace siliconforge::app::localhttp {

struct Url {
    std::string host;  ///< Without the brackets of an IPv6 literal.
    std::string port;
    std::string path;
};

/// `http://host:port/path`, the host a name, an IPv4 address or a bracketed IPv6 one.
[[nodiscard]] inline std::optional<Url> parse(std::string_view url) {
    constexpr std::string_view scheme = "http://";
    if (!url.starts_with(scheme))
        return std::nullopt;
    url.remove_prefix(scheme.size());
    const auto slash     = url.find('/');
    const auto authority = url.substr(0, slash);
    Url        parsed{.host = {}, .port = "80", .path = slash == std::string_view::npos ? "/" : std::string{url.substr(slash)}};
    if (authority.starts_with('[')) {
        const auto close = authority.find(']');
        if (close == std::string_view::npos)
            return std::nullopt;
        parsed.host = authority.substr(1, close - 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':')
                return std::nullopt;
            parsed.port = authority.substr(close + 2);
        }
    } else {
        const auto colon = authority.rfind(':');
        parsed.host      = authority.substr(0, colon);
        if (colon != std::string_view::npos)
            parsed.port = authority.substr(colon + 1);
    }
    if (parsed.host.empty() || parsed.port.empty())
        return std::nullopt;
    return parsed;
}

struct Answer {
    int         status{0};
    std::string body;
};

/// POSTs `body` as application/json with `Authorization: Bearer <token>`, and returns the status and the body of the
/// answer, or why it could not be had (the window is gone, the port refuses...).
[[nodiscard]] inline std::expected<Answer, std::string> post(const Url& url, std::string_view token, std::string_view body) {
#if defined(_WIN32)
    using Socket = SOCKET;
    constexpr Socket invalid = INVALID_SOCKET;
    WSADATA          data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        return std::unexpected(std::string{"Windows sockets are not available"});
    struct Cleanup {
        ~Cleanup() { WSACleanup(); }
    } cleanup;
    const auto closeSocket = [](Socket s) { closesocket(s); };
#else
    using Socket = int;
    constexpr Socket invalid = -1;
    const auto closeSocket = [](Socket s) { ::close(s); };
#endif
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found   = nullptr;
    if (getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &found) != 0 || !found)
        return std::unexpected("Cannot resolve " + url.host);
    Socket socket = invalid;
    for (auto* address = found; address; address = address->ai_next) {
        socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket == invalid)
            continue;
        // Bound connection establishment as well as the subsequent reads. A stale window must not hang the CLI.
#if defined(_WIN32)
        u_long nonblocking = 1;
        ioctlsocket(socket, FIONBIO, &nonblocking);
#else
        const auto flags = fcntl(socket, F_GETFL, 0);
        fcntl(socket, F_SETFL, flags | O_NONBLOCK);
#endif
        auto connected = ::connect(socket, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0;
        if (!connected) {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(socket, &writable);
            timeval timeout{10, 0};
            if (select(static_cast<int>(socket + 1), nullptr, &writable, nullptr, &timeout) > 0) {
                int socketError = 0;
#if defined(_WIN32)
                int length = sizeof(socketError);
                connected = getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &length) == 0 && socketError == 0;
#else
                socklen_t length = sizeof(socketError);
                connected = getsockopt(socket, SOL_SOCKET, SO_ERROR, &socketError, &length) == 0 && socketError == 0;
#endif
            }
        }
        if (connected) {
#if defined(_WIN32)
            nonblocking = 0;
            ioctlsocket(socket, FIONBIO, &nonblocking);
            const DWORD timeout = 60000;
            setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
            fcntl(socket, F_SETFL, flags);
            const timeval timeout{60, 0};
            setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
            const int noSigpipe = 1;
            setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof(noSigpipe));
#endif
#endif
            break;
        }
        closeSocket(socket);
        socket = invalid;
    }
    freeaddrinfo(found);
    if (socket == invalid)
        return std::unexpected("Nothing answers at " + url.host + ":" + url.port);

    const auto host    = url.host.find(':') != std::string::npos ? "[" + url.host + "]" : url.host;
    std::string request = "POST " + url.path + " HTTP/1.1\r\nHost: " + host + ":" + url.port +
                          "\r\nContent-Type: application/json\r\nAccept: application/json\r\nAuthorization: Bearer " + std::string{token} +
                          "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + std::string{body};
    for (std::size_t sent = 0; sent < request.size();) {
#ifdef MSG_NOSIGNAL
        constexpr int sendFlags = MSG_NOSIGNAL;
#else
        constexpr int sendFlags = 0;
#endif
        const auto count = ::send(socket, request.data() + sent, static_cast<int>(request.size() - sent), sendFlags);
        if (count <= 0) {
            closeSocket(socket);
            return std::unexpected(std::string{"The window closed the connection"});
        }
        sent += static_cast<std::size_t>(count);
    }
    std::string answer;
    char        chunk[4096];
    for (;;) {
        const auto count = ::recv(socket, chunk, sizeof chunk, 0);
        if (count < 0) {
            closeSocket(socket);
            return std::unexpected(std::string{"The connection to the window failed"});
        }
        if (count == 0)
            break;
        answer.append(chunk, static_cast<std::size_t>(count));
    }
    closeSocket(socket);

    // "HTTP/1.1 200 OK\r\n...\r\n\r\n<body>"; an interim 100 Continue is not sent: the request carries its body at once.
    const auto separator = answer.find("\r\n\r\n");
    if (!answer.starts_with("HTTP/1.") || answer.size() < 12 || separator == std::string::npos)
        return std::unexpected(std::string{"The window gave an answer that is not HTTP"});
    Answer result;
    result.status = std::atoi(answer.substr(9, 3).c_str());
    result.body   = answer.substr(separator + 4);
    return result;
}

}  // namespace siliconforge::app::localhttp
