#include "AsioServerTransport.h"

#include <asio.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace mm {
namespace net {

using asio::ip::tcp;

// ============================================================================
// PIMPL  –  all asio details are hidden from the header.
// ============================================================================
struct AsioServerTransport::Impl
{
    asio::io_context io_context_;
    tcp::acceptor acceptor_{io_context_};
    std::atomic<bool> running_{false};
    std::atomic<ConnId> next_id_{1};

    struct Client
    {
        tcp::socket socket;
        std::string pending;          // accumulated partial-line data

        explicit Client(tcp::socket&& s) : socket(std::move(s)) {}
    };

    std::mutex clients_mutex_;
    std::unordered_map<ConnId, std::shared_ptr<Client>> clients_;

    // Callbacks
    OnServerConnected    on_connected_;
    OnServerData         on_data_;
    OnServerDisconnected on_disconnected_;

    // ----  async accept loop  ------------------------------------------
    void start_accept()
    {
        auto sock = std::make_shared<tcp::socket>(io_context_);
        acceptor_.async_accept(*sock, [this, sock](asio::error_code ec) {
            if (!ec && running_.load()) {
                const ConnId id = next_id_.fetch_add(1);
                auto client = std::make_shared<Client>(std::move(*sock));
                {
                    std::lock_guard<std::mutex> lock(clients_mutex_);
                    clients_[id] = client;
                }

                if (on_connected_) {
                    on_connected_(id);
                }

                start_read(id, client);
                start_accept();   // post the next accept
            }
        });
    }

    // ----  async per-client read + line-split  -------------------------
    void start_read(ConnId id, std::shared_ptr<Client> client)
    {
        auto buf = std::make_shared<std::array<char, 1024>>();
        client->socket.async_read_some(asio::buffer(*buf),
            [this, id, client, buf](asio::error_code ec, std::size_t bytes) {
                if (ec) {
                    handle_disconnect(id);
                    return;
                }

                client->pending.append(buf->data(), bytes);

                // Line-split on '\n', strip '\r'  (same logic as Winsock)
                size_t nl = std::string::npos;
                while ((nl = client->pending.find('\n')) != std::string::npos) {
                    std::string line = client->pending.substr(0, nl);
                    client->pending.erase(0, nl + 1);
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (on_data_) {
                        on_data_(id, line);
                    }
                }

                start_read(id, client);   // continue reading
            });
    }

    // ----  per-client disconnect  --------------------------------------
    void handle_disconnect(ConnId id)
    {
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            auto it = clients_.find(id);
            if (it != clients_.end()) {
                asio::error_code ec;
                it->second->socket.close(ec);
                clients_.erase(it);
            }
        }
        if (on_disconnected_) {
            on_disconnected_(id);
        }
    }

    // ----  broadcast (must not hold clients_mutex_ across callbacks) ----
    void broadcast_text(const std::string& text)
    {
        std::string msg = text;
        if (msg.empty() || msg.back() != '\n') {
            msg += '\n';
        }

        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto it = clients_.begin(); it != clients_.end();) {
            asio::error_code ec;
            asio::write(it->second->socket, asio::buffer(msg), ec);
            if (ec) {
                it->second->socket.close(ec);
                it = clients_.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    // ----  single-client send  -----------------------------------------
    bool send_text(ConnId id, const std::string& text)
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = clients_.find(id);
        if (it == clients_.end()) {
            return false;
        }

        std::string msg = text;
        if (msg.empty() || msg.back() != '\n') {
            msg += '\n';
        }

        asio::error_code ec;
        asio::write(it->second->socket, asio::buffer(msg), ec);
        return !ec;
    }
};

// ============================================================================
// Public API
// ============================================================================
AsioServerTransport::AsioServerTransport()
  : impl_(std::make_unique<Impl>())
{
}

AsioServerTransport::~AsioServerTransport()
{
    shutdown();
}

bool AsioServerTransport::listen(uint16_t port, const std::string& bind_addr)
{
    asio::error_code ec;
    tcp::endpoint ep(asio::ip::make_address(bind_addr, ec), port);
    if (ec) {
        std::cerr << "[AsioServer] invalid address: " << bind_addr << "\n";
        return false;
    }

    impl_->acceptor_.open(ep.protocol(), ec);
    if (ec) {
        std::cerr << "[AsioServer] open failed\n";
        return false;
    }

    impl_->acceptor_.set_option(tcp::acceptor::reuse_address(true), ec);
    impl_->acceptor_.bind(ep, ec);
    if (ec) {
        std::cerr << "[AsioServer] bind failed on " << bind_addr << ":" << port << "\n";
        return false;
    }

    impl_->acceptor_.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        std::cerr << "[AsioServer] listen failed\n";
        return false;
    }

    return true;
}

void AsioServerTransport::shutdown()
{
    impl_->running_.store(false);

    asio::error_code ec;

    // Close acceptor to stop new connections.
    impl_->acceptor_.close(ec);

    // Close every client socket.
    {
        std::lock_guard<std::mutex> lock(impl_->clients_mutex_);
        for (auto& kv : impl_->clients_) {
            kv.second->socket.close(ec);
        }
        impl_->clients_.clear();
    }

    // Unblock io_context::run().
    impl_->io_context_.stop();
}

void AsioServerTransport::run()
{
    impl_->running_.store(true);

    impl_->start_accept();

    std::cout << "mmdemo server (Asio) listening on 127.0.0.1:7878\n";

    // Block until shutdown() calls io_context_.stop().
    impl_->io_context_.run();
}

bool AsioServerTransport::send(ConnId conn, const std::string& text)
{
    return impl_->send_text(conn, text);
}

void AsioServerTransport::broadcast(const std::string& text)
{
    impl_->broadcast_text(text);
}

void AsioServerTransport::disconnect(ConnId conn)
{
    std::lock_guard<std::mutex> lock(impl_->clients_mutex_);
    auto it = impl_->clients_.find(conn);
    if (it != impl_->clients_.end()) {
        asio::error_code ec;
        it->second->socket.close(ec);
        impl_->clients_.erase(it);
    }
}

void AsioServerTransport::set_on_connected(OnServerConnected cb)
{
    impl_->on_connected_ = std::move(cb);
}

void AsioServerTransport::set_on_data(OnServerData cb)
{
    impl_->on_data_ = std::move(cb);
}

void AsioServerTransport::set_on_disconnected(OnServerDisconnected cb)
{
    impl_->on_disconnected_ = std::move(cb);
}

}  // namespace net
}  // namespace mm
