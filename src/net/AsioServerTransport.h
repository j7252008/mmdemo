#pragma once

#include "ITransport.h"

#include <memory>

namespace mm {
namespace net {

// Concrete server transport using standalone Asio.
//
// Uses asio::io_context for the event loop.  Connection-accept and
// per-client reads are asynchronous.  Line-splitting and '\r'-stripping
// match the Winsock implementation exactly.
//
// Thread safety: broadcast / send / disconnect may be called from any
// thread (most often from the on_data callback running on the io_context
// thread pool).

class AsioServerTransport final : public IServerTransport
{
public:
    AsioServerTransport();
    ~AsioServerTransport() override;

    bool listen(uint16_t port, const std::string& bind_addr = "127.0.0.1") override;
    void shutdown() override;
    void run() override;

    bool send(ConnId conn, const std::string& text) override;
    void broadcast(const std::string& text) override;
    void disconnect(ConnId conn) override;

    void set_on_connected(OnServerConnected cb) override;
    void set_on_data(OnServerData cb) override;
    void set_on_disconnected(OnServerDisconnected cb) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace net
}  // namespace mm
