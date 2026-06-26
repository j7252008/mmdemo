#include "AsioClientTransport.h"

#include <asio.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace mm {
namespace net {

using asio::ip::tcp;

// ============================================================================
// PIMPL
// ============================================================================
struct AsioClientTransport::Impl
{
    asio::io_context io_context_;
    tcp::socket      socket_{io_context_};
    std::atomic<bool> running_{false};
    std::thread       io_thread_;     // runs io_context_

    OnClientData         on_data_;
    OnClientDisconnected on_disconnected_;

    // ----  blocking recv loop (runs on io_thread_)  --------------------
    void recv_loop()
    {
        std::array<char, 1024> buf;
        while (running_.load()) {
            asio::error_code ec;
            const std::size_t n = socket_.read_some(asio::buffer(buf), ec);
            if (ec) {
                running_.store(false);
                break;
            }
            if (on_data_) {
                on_data_(std::string(buf.data(), n));
            }
        }

        // Clean shutdown of the socket.
        asio::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec);
        socket_.close(ec);

        if (on_disconnected_) {
            on_disconnected_();
        }
    }
};

// ============================================================================
// Public API
// ============================================================================
AsioClientTransport::AsioClientTransport()
  : impl_(std::make_unique<Impl>())
{
}

AsioClientTransport::~AsioClientTransport()
{
    disconnect();
}

bool AsioClientTransport::connect(const std::string& host, uint16_t port)
{
    asio::error_code ec;
    tcp::resolver resolver(impl_->io_context_);
    const auto endpoints = resolver.resolve(host, std::to_string(port), ec);
    if (ec) {
        return false;
    }

    asio::connect(impl_->socket_, endpoints, ec);
    if (ec) {
        impl_->socket_.close(ec);
        return false;
    }

    impl_->running_.store(true);
    impl_->io_thread_ = std::thread([this]() { impl_->recv_loop(); });

    return true;
}

void AsioClientTransport::disconnect()
{
    impl_->running_.store(false);

    if (impl_->socket_.is_open()) {
        asio::error_code ec;
        impl_->socket_.shutdown(tcp::socket::shutdown_both, ec);
        impl_->socket_.close(ec);
    }

    impl_->io_context_.stop();

    if (impl_->io_thread_.joinable()) {
        impl_->io_thread_.join();
    }
}

bool AsioClientTransport::send(const std::string& text)
{
    if (!impl_->socket_.is_open()) {
        return false;
    }
    if (text.empty()) {
        return true;
    }

    asio::error_code ec;
    asio::write(impl_->socket_, asio::buffer(text), ec);
    if (ec) {
        return false;
    }

    if (text.back() != '\n') {
        asio::write(impl_->socket_, asio::buffer("\n", 1), ec);
        return !ec;
    }
    return true;
}

void AsioClientTransport::set_on_data(OnClientData cb)
{
    impl_->on_data_ = std::move(cb);
}

void AsioClientTransport::set_on_disconnected(OnClientDisconnected cb)
{
    impl_->on_disconnected_ = std::move(cb);
}

}  // namespace net
}  // namespace mm
