#pragma once

#include "ITransport.h"

#include <memory>

namespace mm {
namespace net {

// Concrete client transport using standalone Asio.
//
// Uses a blocking read loop on a background thread (same pattern as the
// Winsock client).  All socket operations go through asio::ip::tcp::socket.

class AsioClientTransport final : public IClientTransport
{
public:
    AsioClientTransport();
    ~AsioClientTransport() override;

    bool connect(const std::string& host, uint16_t port) override;
    void disconnect() override;
    bool send(const std::string& text) override;

    void set_on_data(OnClientData cb) override;
    void set_on_disconnected(OnClientDisconnected cb) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace net
}  // namespace mm
