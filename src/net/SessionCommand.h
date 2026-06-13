#pragma once

#include <string>

namespace mm {

// Commands that are safe to run before a TCP connection is bound to a player.
bool is_valid_tcp_command_without_login(const std::string& command);

// TCP clients bind to one player after login. This translator lets clients type short commands
// like "pve forest" while still sending explicit player-name commands into GameServer.
std::string translate_session_command(const std::string& line, const std::string& bound_player, std::string& error);

}  // namespace mm
