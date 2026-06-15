#include "SessionCommand.h"

#include "../core/Types.h"

#include <string>
#include <vector>

namespace mm {

namespace {

std::string arg(const std::vector<std::string>& words, size_t index)
{
    return words.size() > index ? words[index] : "";
}

bool is_player_bound_command(const std::string& command)
{
    return command == "pve" || command == "queue" || command == "attack" || command == "heavy"
           || command == "skill" || command == "fire" || command == "defend" || command == "heal"
           || command == "forfeit" || command == "use" || command == "inventory" || command == "bag"
           || command == "buy" || command == "give";
}

bool is_name_command_with_first_player_arg(const std::string& command)
{
    return command == "pve" || command == "queue" || command == "attack" || command == "heavy"
           || command == "skill" || command == "fire" || command == "defend" || command == "heal"
           || command == "forfeit" || command == "use" || command == "inventory" || command == "bag"
           || command == "buy" || command == "give";
}

}  // namespace

bool is_valid_tcp_command_without_login(const std::string& command)
{
    return command == "help" || command == "monsters" || command == "items" || command == "skills"
           || command == "quests" || command == "shop" || command == "state" || command == "log"
           || command == "load";
}

std::string translate_session_command(const std::string& line, const std::string& bound_player,
                                      std::string& error)
{
    const auto words = split_words(line);
    if (words.empty()) {
        return line;
    }

    const std::string& command = words[0];
    if (command == "quest") {
        const std::string subcommand = arg(words, 1);
        if (subcommand == "accept" || subcommand == "claim") {
            if (words.size() == 3) {
                return "quest " + subcommand + " " + bound_player + " " + words[2];
            }
            if (words.size() >= 4 && words[2] == bound_player) {
                return line;
            }
            error = "[error] this session is bound to " + bound_player + "\n";
            return "";
        }
        if (subcommand == "list") {
            if (words.size() == 2) {
                return "quest list " + bound_player;
            }
            if (words.size() >= 3 && words[2] == bound_player) {
                return line;
            }
            error = "[error] this session is bound to " + bound_player + "\n";
            return "";
        }
        return line;
    }

    if (!is_player_bound_command(command)) {
        return line;
    }

    if (words.size() >= 2 && words[1] == bound_player) {
        return line;
    }
    if (is_name_command_with_first_player_arg(command) && words.size() >= 2
        && words[1] != bound_player) {
        if ((command == "pve" && words.size() == 2)
            || ((command == "attack" || command == "heavy" || command == "skill"
                 || command == "fire" || command == "heal")
                && words.size() == 2)
            || (command == "use" && words.size() == 2)
            || ((command == "buy" || command == "give") && words.size() >= 2)) {
            // These are shorthand forms where the first argument is not a player name.
        }
        else {
            error = "[error] this session is bound to " + bound_player + "\n";
            return "";
        }
    }

    if (command == "queue" || command == "inventory" || command == "bag" || command == "forfeit") {
        return command + " " + bound_player;
    }
    if (command == "pve") {
        return "pve " + bound_player + (words.size() >= 2 ? " " + words[1] : "");
    }
    if (command == "use") {
        return "use " + bound_player + (words.size() >= 2 ? " " + words[1] : "")
               + (words.size() >= 3 ? " " + words[2] : "");
    }
    if (command == "buy" || command == "give") {
        return command + " " + bound_player + (words.size() >= 2 ? " " + words[1] : "")
               + (words.size() >= 3 ? " " + words[2] : "");
    }

    return command + " " + bound_player + (words.size() >= 2 ? " " + words[1] : "");
}

}  // namespace mm
