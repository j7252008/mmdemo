#include "game_core.h"
#include "net/GameSessionHandler.h"
#include "net/ITransport.h"

#include <iostream>

int main()
{
    try {
        mm::GameServer game;

        // ---- Choose transport backend here ----
        // To switch to another network library (asio, libuv, posix, …),
        // just change the enum value – no other code needs to change.
        auto transport = mm::net::create_server_transport(
            mm::net::TransportBackend::Asio);

        if (!transport) {
            std::cerr << "failed to create server transport\n";
            return 1;
        }

        mm::net::GameSessionHandler sessions(game);

        // Wire callbacks: transport → session handler.
        transport->set_on_connected([&](mm::net::ConnId conn) {
            std::string greeting = sessions.on_connect(conn);
            transport->send(conn, greeting);
        });

        transport->set_on_data([&](mm::net::ConnId conn, const std::string& line) {
            auto result = sessions.on_message(conn, line);
            if (!result.response.empty()) {
                transport->broadcast(result.response);
            }
            if (result.disconnect_client) {
                transport->send(conn, "[server] goodbye\n");
                transport->disconnect(conn);
            }
        });

        transport->set_on_disconnected([&](mm::net::ConnId conn) {
            std::string text = sessions.on_disconnect(conn);
            if (!text.empty()) {
                transport->broadcast(text);
            }
        });

        if (!transport->listen(7878)) {
            return 1;
        }

        transport->run();
    }
    catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
