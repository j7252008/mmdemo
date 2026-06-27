#include "AsioServerTransport.h"

#include <asio.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

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
    std::string bind_address_ = "127.0.0.1";
    uint16_t port_ = 0;

    struct Client
    {
        tcp::socket socket;
        std::string pending;              // accumulated partial-line data
        std::deque<std::string> outgoing; // serialized by io_context_
        bool writing = false;
        bool close_after_write = false;
        bool notify_disconnect = true;

        explicit Client(tcp::socket&& s) : socket(std::move(s)) {}
    };

    std::mutex clients_mutex_;
    std::unordered_map<ConnId, std::shared_ptr<Client>> clients_;

    // Callbacks
    OnServerConnected    on_connected_;
    OnServerData         on_data_;
    OnServerDisconnected on_disconnected_;

    static std::string with_newline(std::string text)
    {
        if (text.empty() || text.back() != '\n') {
            text += '\n';
        }
        return text;
    }

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

    // ----  async per-client write queue  -------------------------------
    void enqueue_text(ConnId id, const std::shared_ptr<Client>& client, std::string text)
    {
        if (!running_.load() || client->close_after_write) {
            return;
        }

        client->outgoing.push_back(std::move(text));
        if (!client->writing) {
            start_write(id, client);
        }
    }

    void start_write(ConnId id, const std::shared_ptr<Client>& client)
    {
        if (client->outgoing.empty()) {
            client->writing = false;
            if (client->close_after_write) {
                close_client(id);
            }
            return;
        }

        client->writing = true;
        asio::async_write(client->socket, asio::buffer(client->outgoing.front()),
            [this, id, client](asio::error_code ec, std::size_t /*bytes*/) {
                if (ec) {
                    close_client(id);
                    return;
                }

                client->outgoing.pop_front();
                start_write(id, client);
            });
    }

    void close_after_pending_writes(ConnId id)
    {
        std::shared_ptr<Client> client;
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            auto it = clients_.find(id);
            if (it == clients_.end()) {
                return;
            }
            client = it->second;
        }

        asio::post(io_context_, [this, id, client]() {
            client->notify_disconnect = false;
            client->close_after_write = true;
            if (!client->writing && client->outgoing.empty()) {
                close_client(id);
            }
        });
    }

    // ----  per-client disconnect  --------------------------------------
    void close_client(ConnId id)
    {
        bool notify = false;
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            auto it = clients_.find(id);
            if (it != clients_.end()) {
                notify = it->second->notify_disconnect;
                asio::error_code ec;
                it->second->socket.close(ec);
                clients_.erase(it);
            }
        }
        if (notify && on_disconnected_) {
            on_disconnected_(id);
        }
    }

    void handle_disconnect(ConnId id)
    {
        close_client(id);
    }

    // ----  broadcast (queue writes without blocking io_context_) --------
    void broadcast_text(const std::string& text)
    {
        const std::string msg = with_newline(text);
        std::vector<std::pair<ConnId, std::shared_ptr<Client>>> clients;

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients.reserve(clients_.size());
            for (const auto& pair : clients_) {
                clients.emplace_back(pair.first, pair.second);
            }
        }

        for (const auto& pair : clients) {
            asio::post(io_context_, [this, id = pair.first, client = pair.second, msg]() {
                enqueue_text(id, client, msg);
            });
        }
    }

    // ----  single-client send  -----------------------------------------
    bool send_text(ConnId id, const std::string& text)
    {
        std::shared_ptr<Client> client;
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            auto it = clients_.find(id);
            if (it == clients_.end()) {
                return false;
            }
            client = it->second;
        }

        std::string msg = with_newline(text);
        asio::post(io_context_, [this, id, client, msg = std::move(msg)]() mutable {
            enqueue_text(id, client, std::move(msg));
        });
        return true;
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

    impl_->bind_address_ = bind_addr;
    impl_->port_ = port;
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

    std::cout << "mmdemo server (Asio) listening on " << impl_->bind_address_ << ":"
              << impl_->port_ << "\n";

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
    impl_->close_after_pending_writes(conn);
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
