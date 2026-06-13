#pragma once

#include "../battle/Battle.h"
#include "../core/Output.h"
#include "../core/Types.h"

#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace mm {

// Command-level game facade. It owns players, matchmaking, active battles, rewards, quests,
// persistence, and the text command protocol used by both local CLI and TCP server.
class GameServer
{
public:
    GameServer();
    explicit GameServer(std::string player_file, unsigned int seed = 1);
    GameServer(std::string player_file, unsigned int seed, std::string config_file);

    // Test/TCP-friendly command entry point. It serializes access and captures the text response.
    std::string execute(const std::string& line);

    // Local debug loop. TCP uses execute() instead so the socket layer can manage sessions.
    void run();

private:
    // Central command router. Command handlers stay private so protocol validation remains in one place.
    void handle_line(const std::string& line, bool allow_process_exit);

    static std::string arg(const std::vector<std::string>& words, size_t index);

    void login(const std::string& name);
    void logout(const std::string& name);
    void forfeit(const std::string& name);
    void start_pve(const std::string& name, const std::string& encounter_id);
    void queue(const std::string& name);
    void submit_action(const std::string& name, const std::string& skill, const std::string& target);
    void submit_item_action(const std::string& name, const std::string& item_id, const std::string& target);
    void create_battle(const std::string& mode, std::vector<Fighter> fighters);
    void finish_battle(Battle& battle);
    void grant_rewards(const Battle& battle);
    bool grant_player_reward(Player& player, int exp, int gold, const std::map<std::string, int>& items);
    void update_quest_progress(Player& player, const std::map<std::string, int>& kills);

    static int exp_to_next(int level);
    Fighter make_player_fighter(const Player& player, const std::string& side) const;
    static Fighter make_monster_fighter(const MonsterDef& def, int index, const std::string& side);
    Player* require_player(const std::string& name);
    Battle* active_battle(const std::string& id);
    void remove_from_queue(const std::string& name);
    std::string pop_waiting_opponent(const std::string& self);

    void print_players() const;
    void give_item(const std::string& name, const std::string& item_id, const std::string& amount_text);
    void print_inventory(const std::string& name) const;
    std::string inventory_summary(const Player& player) const;
    void print_monsters() const;
    void print_items() const;
    void print_shop() const;
    void buy_item(const std::string& name, const std::string& item_id, const std::string& amount_text);
    void print_quest_defs() const;
    void handle_quest_command(const std::string& subcmd, const std::string& name, const std::string& quest_id);
    void accept_quest(const std::string& name, const std::string& quest_id);
    void print_player_quests(const std::string& name) const;
    void claim_quest(const std::string& name, const std::string& quest_id);
    std::string quest_summary(const Player& player) const;

    static std::string item_map_summary(const std::map<std::string, int>& items);
    static std::string drop_summary(const MonsterDef& monster);
    static std::string join_ids(const std::vector<std::string>& values);

    void print_skills() const;
    void print_state() const;
    void print_log(const std::string& id) const;
    void save_players() const;
    void load_players();
    void print_help() const;

    Config config_;
    std::mt19937 rng_;
    std::map<std::string, Player> players_;
    std::vector<std::string> waiting_queue_;
    std::map<std::string, std::unique_ptr<Battle>> battles_;
    std::map<std::string, std::unique_ptr<Battle>> history_;
    int next_battle_id_ = 1;
    std::mutex mutex_;
    std::string player_file_ = "data/players.json";
    OutputSink out_;
};

}  // namespace mm
