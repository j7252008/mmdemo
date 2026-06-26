#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace mm {
namespace net {

// ============================================================================
// Opaque connection identifier used by the transport layer.
// ============================================================================
using ConnId = uint64_t;

// ============================================================================
// Transport configuration – structure that stays the same regardless of which
// network library backs the transport.  Add fields here (backward-compatibly)
// when new transports need them (TLS certs, WebSocket paths, etc.).
// ============================================================================
struct ServerTransportConfig
{
    std::string bind_address = "127.0.0.1";
    uint16_t port            = 0;
    int max_connections      = 64;
};

struct ClientTransportConfig
{
    std::string host = "127.0.0.1";
    uint16_t port    = 0;
};

// ============================================================================
// Transport backend tag – identifies which network library to use.
// Add new values when you plug in asio, libuv, raw POSIX sockets, etc.
// ============================================================================
enum class TransportBackend
{
    Asio,   // Standalone Asio (cross-platform, header-only)
};

// ============================================================================
// Server transport callbacks (text-line-level, '\\n'-delimited).
// ============================================================================
using OnServerConnected    = std::function<void(ConnId)>;
using OnServerData         = std::function<void(ConnId, const std::string& /*line*/)>;
using OnServerDisconnected = std::function<void(ConnId)>;

// ============================================================================
// Abstract server transport.
//
// Accepts clients, delivers complete text lines (split on '\\n'), and
// broadcasts results.  Implementations hide platform sockets / event loops
// behind this interface.
//
// How to switch backends:
//   auto transport = create_server_transport(TransportBackend::Winsock);
//   transport->listen(7878);
//   transport->run();
// ============================================================================
class IServerTransport
{
public:
    virtual ~IServerTransport() = default;

    // Start listening. Blocks inside run() until shutdown() is called from
    // another thread.  Returns false on bind/listen failure.
    virtual bool listen(uint16_t port, const std::string& bind_addr = "127.0.0.1") = 0;

    // Graceful stop: close listener, close all client sockets, join worker
    // threads.
    virtual void shutdown() = 0;

    // Enter the accept loop. Returns when shutdown() is called.
    virtual void run() = 0;

    // Send a text line to one client (newline appended automatically).
    virtual bool send(ConnId conn, const std::string& text) = 0;

    // Broadcast a text line to every connected client.
    virtual void broadcast(const std::string& text) = 0;

    // Forcibly close a connection.
    virtual void disconnect(ConnId conn) = 0;

    // Register callbacks. Must be set before run().
    virtual void set_on_connected(OnServerConnected cb)       = 0;
    virtual void set_on_data(OnServerData cb)                 = 0;
    virtual void set_on_disconnected(OnServerDisconnected cb) = 0;
};

// ============================================================================
// Client transport callbacks.
// ============================================================================
using OnClientData         = std::function<void(const std::string&)>;
using OnClientDisconnected = std::function<void()>;

// ============================================================================
// Abstract client transport.
//
// Connects to a server and delivers received text to a callback.
//
// How to switch backends:
//   auto transport = create_client_transport(TransportBackend::Winsock);
//   transport->connect("127.0.0.1", 7878);
// ============================================================================
class IClientTransport
{
public:
    virtual ~IClientTransport() = default;

    // Connect to host:port. Returns false on failure.
    virtual bool connect(const std::string& host, uint16_t port) = 0;

    // Graceful disconnect: shutdown socket, join receive thread.
    virtual void disconnect() = 0;

    // Send a text line (newline appended automatically).
    virtual bool send(const std::string& text) = 0;

    // Register callbacks.
    virtual void set_on_data(OnClientData cb)                 = 0;
    virtual void set_on_disconnected(OnClientDisconnected cb) = 0;
};

// ============================================================================
// Transport factory – the *only* place that maps backend tags to concrete
// implementations.  User code never #includes platform-specific headers.
// ============================================================================
std::unique_ptr<IServerTransport> create_server_transport(TransportBackend backend);
std::unique_ptr<IClientTransport> create_client_transport(TransportBackend backend);

}  // namespace net
}  // namespace mm
