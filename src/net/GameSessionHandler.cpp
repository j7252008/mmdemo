#include "GameSessionHandler.h"

#include "../core/Types.h"
#include "../game/GameServer.h"
#include "SessionCommand.h"

#include <string>

namespace mm {
namespace net {

GameSessionHandler::GameSessionHandler(GameServer& game)
  : game_(game)
{
}

std::string GameSessionHandler::on_connect(ConnId /*conn*/)
{
    return "mmdemo TCP text client connected.\n"
           "Type login <name> first. After login, use shorthand: pve slime, queue, "
           "heavy bob, use potion.\n"
           "Type quit to close this client session.\n";
}

MessageResult GameSessionHandler::on_message(ConnId conn, const std::string& line)
{
    // Ensure a session record exists.
    Session& session = sessions_[conn];

    // ---- quit / exit ----
    if (line == "quit" || line == "exit") {
        std::string logout_text = do_logout(conn);
        sessions_.erase(conn);
        return MessageResult{ logout_text, true };
    }

    // ---- Command processing ----
    const auto words = split_words(line);
    const std::string command = words.empty() ? "" : words[0];

    if (command == "login") {
        if (!session.bound_player.empty()) {
            return MessageResult{
                "[error] this session is already logged in as " + session.bound_player + "\n",
                false };
        }
        if (words.size() < 2) {
            return MessageResult{ "[error] login requires a player name\n", false };
        }
    }
    else if (session.bound_player.empty()
             && !mm::is_valid_tcp_command_without_login(command)) {
        return MessageResult{ "[error] login first: login <name>\n", false };
    }

    // ---- Session command translation ----
    std::string translated = line;
    if (!session.bound_player.empty()) {
        std::string error;
        translated = mm::translate_session_command(line, session.bound_player, error);
        if (!error.empty()) {
            return MessageResult{ error, false };
        }
    }

    // ---- Dispatch to game server ----
    std::string response = game_.execute(translated);

    // ---- Track login binding ----
    if (command == "login"
        && (response.find("[server] " + words[1] + " entered the world") != std::string::npos
            || response.find("[server] " + words[1] + " reconnected") != std::string::npos)) {
        session.bound_player = words[1];
        response += "[session] bound to " + session.bound_player + "\n";
    }

    if (response.empty()) {
        response = "[server] ok\n";
    }

    return MessageResult{ response, false };
}

std::string GameSessionHandler::on_disconnect(ConnId conn)
{
    std::string result = do_logout(conn);
    sessions_.erase(conn);
    return result;
}

std::string GameSessionHandler::do_logout(ConnId conn)
{
    auto it = sessions_.find(conn);
    if (it == sessions_.end() || it->second.bound_player.empty()) {
        return {};
    }

    const std::string& player = it->second.bound_player;

    std::string response = game_.execute("logout " + player);
    if (response.find("[error] " + player + " is in battle, cannot logout")
        != std::string::npos) {
        response += game_.execute("forfeit " + player);
        response += game_.execute("logout " + player);
    }

    if (response.empty()) {
        response = "[server] ok\n";
    }

    // If logout succeeded, clear the binding so on_disconnect doesn't repeat.
    if (response.find("[server] " + player + " logged out") != std::string::npos) {
        it->second.bound_player.clear();
    }

    return response;
}

}  // namespace net
}  // namespace mm
