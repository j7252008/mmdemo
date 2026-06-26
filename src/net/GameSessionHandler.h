#pragma once

#include "ITransport.h"
#include <string>
#include <unordered_map>

namespace mm {

class GameServer;

namespace net {

// Result returned from ISessionHandler::on_message().
// The transport broadcasts `response` to all clients.
// When `disconnect_client` is true the transport also sends
// "[server] goodbye\n" and closes the connection.
struct MessageResult
{
    std::string response;
    bool disconnect_client = false;
};

// Abstract session handler called by the transport layer.
// Implementations translate raw text lines into domain commands
// and produce responses – they do NOT touch sockets.
class ISessionHandler
{
public:
    virtual ~ISessionHandler() = default;

    // Called when a new client connects. Returns a greeting line sent only to that client.
    virtual std::string on_connect(ConnId conn) = 0;

    // Called for each complete text line received from a client.
    virtual MessageResult on_message(ConnId conn, const std::string& line) = 0;

    // Called when a client disconnects unexpectedly (recv failure).
    // The returned string is broadcast to remaining clients by the transport.
    virtual std::string on_disconnect(ConnId conn) = 0;
};

// Concrete session handler that bridges the text transport to GameServer.
//
// Responsibilities:
//  - Tracks per-connection bound-player state.
//  - Translates shorthand TCP commands via SessionCommand.
//  - Dispatches to GameServer::execute().
//  - Handles quit / logout / forfeit-on-disconnect.
class GameSessionHandler final : public ISessionHandler
{
public:
    explicit GameSessionHandler(GameServer& game);

    std::string on_connect(ConnId conn) override;
    MessageResult on_message(ConnId conn, const std::string& line) override;
    std::string on_disconnect(ConnId conn) override;

private:
    GameServer& game_;

    // Per-connection session state.
    struct Session
    {
        std::string bound_player;
    };
    std::unordered_map<ConnId, Session> sessions_;

    // Shared logout logic used by both quit and disconnect paths.
    std::string do_logout(ConnId conn);
};

}  // namespace net
}  // namespace mm
