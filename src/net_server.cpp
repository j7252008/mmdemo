#define MMDEMO_NO_MAIN
#include "game_core.h"
#include "net/SessionCommand.h"

#ifndef _WIN32
#error "This prototype TCP server uses Winsock and currently targets Windows."
#endif

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr unsigned short kPort = 7878;

// RAII wrapper for the process-wide Winsock lifetime used by the prototype server.
class WinsockRuntime
{
public:
    WinsockRuntime()
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }

    ~WinsockRuntime() { WSACleanup(); }
};

// Best-effort full-buffer send. The text protocol is small, but send() may still write partially.
bool send_all(SOCKET socket, const std::string& text)
{
    size_t sent = 0;
    while (sent < text.size()) {
        const int chunk = send(socket, text.data() + sent, static_cast<int>(text.size() - sent), 0);
        if (chunk == SOCKET_ERROR || chunk == 0) {
            return false;
        }
        sent += static_cast<size_t>(chunk);
    }
    return true;
}

bool send_text(SOCKET socket, const std::string& text)
{
    if (text.empty()) {
        return true;
    }
    if (!send_all(socket, text)) {
        return false;
    }
    if (text.back() != '\n') {
        return send_all(socket, "\n");
    }
    return true;
}

// Connected clients share the same text battle feed in this prototype.
class ClientHub
{
public:
    void add(SOCKET socket)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.push_back(socket);
    }

    void remove(SOCKET socket)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.erase(std::remove(clients_.begin(), clients_.end(), socket), clients_.end());
    }

    void broadcast(const std::string& text)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = clients_.begin(); it != clients_.end();) {
            if (!send_text(*it, text)) {
                closesocket(*it);
                it = clients_.erase(it);
            }
            else {
                ++it;
            }
        }
    }

private:
    std::mutex mutex_;
    std::vector<SOCKET> clients_;
};

// Disconnect/quit should not leave a player stuck online or inside a battle room.
void logout_bound_player(mm::GameServer& game, ClientHub& hub, std::string& bound_player)
{
    if (bound_player.empty()) {
        return;
    }
    std::string response = game.execute("logout " + bound_player);
    if (response.find("[error] " + bound_player + " is in battle, cannot logout") != std::string::npos) {
        response += game.execute("forfeit " + bound_player);
        response += game.execute("logout " + bound_player);
    }
    hub.broadcast(response.empty() ? "[server] ok\n" : response);
    if (response.find("[server] " + bound_player + " logged out") != std::string::npos) {
        bound_player.clear();
    }
}

// One thread owns one socket. Shared game state is protected inside GameServer::execute().
void handle_client(SOCKET client, mm::GameServer& game, ClientHub& hub)
{
    hub.add(client);
    send_text(client, "mmdemo TCP text client connected.\n"
                      "Type login <name> first. After login, use shorthand: pve slime, queue, heavy bob, use potion.\n"
                      "Type quit to close this client session.\n");

    std::string pending;
    std::string bound_player;
    char buffer[1024];

    while (true) {
        const int received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            break;
        }

        pending.append(buffer, buffer + received);
        size_t newline = std::string::npos;
        while ((newline = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (line == "quit" || line == "exit") {
                logout_bound_player(game, hub, bound_player);
                send_text(client, "[server] goodbye\n");
                hub.remove(client);
                closesocket(client);
                return;
            }

            const auto words = mm::split_words(line);
            std::string command = words.empty() ? "" : words[0];
            std::string translated = line;
            if (command == "login") {
                if (!bound_player.empty()) {
                    send_text(client, "[error] this session is already logged in as " + bound_player + "\n");
                    continue;
                }
                if (words.size() < 2) {
                    send_text(client, "[error] login requires a player name\n");
                    continue;
                }
            }
            else if (bound_player.empty() && !mm::is_valid_tcp_command_without_login(command)) {
                send_text(client, "[error] login first: login <name>\n");
                continue;
            }
            else if (!bound_player.empty()) {
                std::string error;
                translated = mm::translate_session_command(line, bound_player, error);
                if (!error.empty()) {
                    send_text(client, error);
                    continue;
                }
            }

            // GameServer returns the authoritative text result; the socket layer only broadcasts it.
            std::string response = game.execute(translated);
            if (command == "login"
                && (response.find("[server] " + words[1] + " entered the world") != std::string::npos
                    || response.find("[server] " + words[1] + " reconnected") != std::string::npos)) {
                bound_player = words[1];
                response += "[session] bound to " + bound_player + "\n";
            }
            hub.broadcast(response.empty() ? "[server] ok\n" : response);
        }
    }

    logout_bound_player(game, hub, bound_player);
    hub.remove(client);
    closesocket(client);
}

}  // namespace

int main()
{
    try {
        WinsockRuntime winsock;
        mm::GameServer game;
        ClientHub hub;

        SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET) {
            std::cerr << "socket failed\n";
            return 1;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(kPort);

        if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            std::cerr << "bind failed on 127.0.0.1:" << kPort << "\n";
            closesocket(listener);
            return 1;
        }

        if (listen(listener, SOMAXCONN) == SOCKET_ERROR) {
            std::cerr << "listen failed\n";
            closesocket(listener);
            return 1;
        }

        std::cout << "mmdemo server listening on 127.0.0.1:" << kPort << "\n";
        while (true) {
            SOCKET client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                continue;
            }
            std::thread(handle_client, client, std::ref(game), std::ref(hub)).detach();
        }
    }
    catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << "\n";
        return 1;
    }
}
