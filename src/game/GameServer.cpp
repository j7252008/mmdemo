#include "GameServer.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <utility>

namespace mm {

GameServer::GameServer()
  : config_(Config::load_json_or_default("data/config.json")), rng_(std::random_device{}())
{}

GameServer::GameServer(std::string player_file, unsigned int seed)
  : config_(Config::make_default()), rng_(seed), player_file_(std::move(player_file))
{}

GameServer::GameServer(std::string player_file, unsigned int seed, std::string config_file)
  : config_(config_file.empty() ? Config::make_default()
                                : Config::load_json_or_default(config_file))
  , rng_(seed)
  , player_file_(std::move(player_file))
{}

std::string GameServer::execute(const std::string& line)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream output;
    ScopedOutputRedirect redirect(out_, output);
    handle_line(line, false);
    return output.str();
}

void GameServer::run()
{
    out_ << "mmdemo C++17 text MMO prototype\n";
    print_help();

    std::string line;
    while (out_ << "> " && std::getline(std::cin, line)) {
        handle_line(line, true);
    }
}

void GameServer::handle_line(const std::string& line, bool allow_process_exit)
{
    const auto words = split_words(line);
    if (words.empty()) {
        return;
    }

    const std::string& cmd = words[0];
    if (cmd == "help") {
        print_help();
    }
    else if (cmd == "quit" || cmd == "exit") {
        if (allow_process_exit) {
            std::exit(0);
        }
        out_ << "[server] goodbye\n";
    }
    else if (cmd == "login") {
        login(arg(words, 1));
    }
    else if (cmd == "logout") {
        logout(arg(words, 1));
    }
    else if (cmd == "forfeit") {
        forfeit(arg(words, 1));
    }
    else if (cmd == "pve") {
        start_pve(arg(words, 1), words.size() > 2 ? words[2] : "slime");
    }
    else if (cmd == "queue") {
        queue(arg(words, 1));
    }
    else if (cmd == "attack" || cmd == "heavy" || cmd == "skill" || cmd == "fire" || cmd == "defend"
             || cmd == "heal") {
        const std::string skill = cmd == "skill" ? "heavy" : cmd;
        submit_action(arg(words, 1), skill, arg(words, 2));
    }
    else if (cmd == "use") {
        submit_item_action(arg(words, 1), arg(words, 2), arg(words, 3));
    }
    else if (cmd == "give") {
        give_item(arg(words, 1), arg(words, 2), arg(words, 3));
    }
    else if (cmd == "inventory" || cmd == "bag") {
        print_inventory(arg(words, 1));
    }
    else if (cmd == "players") {
        print_players();
    }
    else if (cmd == "monsters") {
        print_monsters();
    }
    else if (cmd == "items") {
        print_items();
    }
    else if (cmd == "shop") {
        print_shop();
    }
    else if (cmd == "buy") {
        buy_item(arg(words, 1), arg(words, 2), arg(words, 3));
    }
    else if (cmd == "quests") {
        print_quest_defs();
    }
    else if (cmd == "quest") {
        handle_quest_command(arg(words, 1), arg(words, 2), arg(words, 3));
    }
    else if (cmd == "skills") {
        print_skills();
    }
    else if (cmd == "state") {
        print_state();
    }
    else if (cmd == "log") {
        print_log(arg(words, 1));
    }
    else if (cmd == "save") {
        save_players();
    }
    else if (cmd == "load") {
        load_players();
    }
    else {
        out_ << "[error] unknown command, type help\n";
    }
}

std::string GameServer::arg(const std::vector<std::string>& words, size_t index)
{
    return words.size() > index ? words[index] : "";
}

void GameServer::login(const std::string& name)
{
    if (name.empty()) {
        out_ << "[error] name is required\n";
        return;
    }
    auto player_it = players_.find(name);
    if (player_it != players_.end() && player_it->second.online) {
        out_ << "[error] " << name << " is already online\n";
        return;
    }
    if (player_it != players_.end()) {
        player_it->second.online = true;
        out_ << "[server] " << name << " reconnected\n";
    }
    else {
        players_.emplace(name, Player{ name });
        out_ << "[server] " << name << " entered the world\n";
    }
}

void GameServer::logout(const std::string& name)
{
    Player* player = require_player(name);
    if (player == nullptr) {
        return;
    }
    if (!player->battle_id.empty()) {
        out_ << "[error] " << name << " is in battle, cannot logout\n";
        return;
    }
    remove_from_queue(name);
    player->online = false;
    out_ << "[server] " << name << " logged out\n";
}

void GameServer::forfeit(const std::string& name)
{
    Player* player = require_player(name);
    if (player == nullptr) {
        return;
    }
    if (player->battle_id.empty()) {
        out_ << "[error] " << name << " is not in battle\n";
        return;
    }

    Battle* battle = active_battle(player->battle_id);
    if (battle == nullptr) {
        player->battle_id.clear();
        out_ << "[error] battle disappeared\n";
        return;
    }

    std::string error;
    if (!battle->forfeit_player(name, error)) {
        out_ << "[error] " << error << "\n";
        return;
    }
    if (battle->closed()) {
        finish_battle(*battle);
    }
}

void GameServer::start_pve(const std::string& name, const std::string& encounter_id)
{
    Player* player = require_player(name);
    if (player == nullptr) {
        return;
    }
    if (!player->battle_id.empty()) {
        out_ << "[error] " << name << " is already in battle\n";
        return;
    }
    const auto encounter_it = config_.encounters.find(encounter_id);
    if (encounter_it == config_.encounters.end()) {
        out_ << "[error] unknown encounter: " << encounter_id << "\n";
        return;
    }

    remove_from_queue(name);
    std::vector<Fighter> fighters;
    fighters.push_back(make_player_fighter(*player, "left"));
    int monster_index = 1;
    for (const auto& monster_id : encounter_it->second.monsters) {
        const auto monster_it = config_.monsters.find(monster_id);
        if (monster_it == config_.monsters.end()) {
            out_ << "[error] encounter " << encounter_id
                 << " references unknown monster: " << monster_id << "\n";
            return;
        }
        fighters.push_back(make_monster_fighter(monster_it->second, monster_index++, "right"));
    }
    out_ << "[server] " << name << " enters " << encounter_it->second.name << "\n";
    create_battle("pve", fighters);
}

void GameServer::queue(const std::string& name)
{
    Player* player = require_player(name);
    if (player == nullptr) {
        return;
    }
    if (!player->battle_id.empty()) {
        out_ << "[error] " << name << " is already in battle\n";
        return;
    }
    if (player->queued) {
        out_ << "[server] " << name << " is already queued\n";
        return;
    }

    const std::string opponent_name = pop_waiting_opponent(name);
    if (opponent_name.empty()) {
        player->queued = true;
        waiting_queue_.push_back(name);
        out_ << "[server] " << name << " queued\n";
        return;
    }

    Player& opponent = players_.at(opponent_name);
    player->queued = false;
    opponent.queued = false;

    std::vector<Fighter> fighters;
    fighters.push_back(make_player_fighter(opponent, "left"));
    fighters.push_back(make_player_fighter(*player, "right"));
    create_battle("pvp", fighters);
}

void GameServer::submit_action(const std::string& name, const std::string& skill,
                               const std::string& target)
{
    Player* player = require_player(name);
    if (player == nullptr) {
        return;
    }
    if (player->battle_id.empty()) {
        out_ << "[error] " << name << " is not in battle\n";
        return;
    }
    Battle* battle = active_battle(player->battle_id);
    if (battle == nullptr) {
        player->battle_id.clear();
        out_ << "[error] battle disappeared\n";
        return;
    }

    std::string error;
    if (!battle->submit_action(name, skill, target, error)) {
        out_ << "[error] " << error << "\n";
        return;
    }
    if (battle->closed()) {
        finish_battle(*battle);
    }
}

void GameServer::submit_item_action(const std::string& name, const std::string& item_id,
                                    const std::string& target)
{
    Player* player = require_player(name);
    if (player == nullptr) {
        return;
    }
    if (item_id.empty()) {
        out_ << "[error] item id is required\n";
        return;
    }
    if (player->battle_id.empty()) {
        out_ << "[error] " << name << " is not in battle\n";
        return;
    }
    const auto item_it = config_.items.find(item_id);
    if (item_it == config_.items.end()) {
        out_ << "[error] unknown item: " << item_id << "\n";
        return;
    }
    auto inv_it = player->inventory.find(item_id);
    if (inv_it == player->inventory.end() || inv_it->second <= 0) {
        out_ << "[error] " << name << " does not have " << item_id << "\n";
        return;
    }

    Battle* battle = active_battle(player->battle_id);
    if (battle == nullptr) {
        player->battle_id.clear();
        out_ << "[error] battle disappeared\n";
        return;
    }

    std::string error;
    if (!battle->submit_item_action(name, item_id, target, error)) {
        out_ << "[error] " << error << "\n";
        return;
    }

    --inv_it->second;
    if (inv_it->second == 0) {
        player->inventory.erase(inv_it);
    }

    if (battle->closed()) {
        finish_battle(*battle);
    }
}

void GameServer::create_battle(const std::string& mode, std::vector<Fighter> fighters)
{
    const std::string id = "B" + std::to_string(next_battle_id_++);
    for (const auto& fighter : fighters) {
        if (fighter.is_player) {
            players_.at(fighter.player_name).battle_id = id;
        }
    }

    auto battle = std::make_unique<Battle>(id, mode, std::move(fighters), config_, rng_, out_);

    Battle* raw = battle.get();
    battles_.emplace(id, std::move(battle));
    raw->start();
    if (raw->closed()) {
        finish_battle(*raw);
    }
}

void GameServer::finish_battle(Battle& battle)
{
    grant_rewards(battle);

    for (const auto& player_name : battle.player_names()) {
        players_.at(player_name).battle_id.clear();
    }

    const std::string id = battle.id();
    const auto it = battles_.find(id);
    if (it != battles_.end()) {
        history_[id] = std::move(it->second);
        battles_.erase(it);
    }
}

void GameServer::grant_rewards(const Battle& battle)
{
    if (battle.mode() != "pve") {
        return;
    }

    const auto& fighters = battle.fighters();
    const bool player_side_alive =
      std::any_of(fighters.begin(), fighters.end(),
                  [](const Fighter& f) { return f.is_player && f.side == "left" && f.alive(); });
    const bool monster_side_alive =
      std::any_of(fighters.begin(), fighters.end(),
                  [](const Fighter& f) { return !f.is_player && f.side == "right" && f.alive(); });
    if (!player_side_alive || monster_side_alive) {
        return;
    }

    int exp = 0;
    int gold = 0;
    std::map<std::string, int> drops;
    std::map<std::string, int> kills;
    for (const auto& fighter : fighters) {
        if (!fighter.is_player) {
            exp += fighter.exp;
            gold += fighter.gold;
            kills[fighter.monster_id] += 1;
            const auto monster_it = config_.monsters.find(fighter.monster_id);
            if (monster_it != config_.monsters.end()) {
                for (const auto& drop : monster_it->second.drops) {
                    drops[drop.first] += drop.second;
                }
            }
        }
    }

    for (const auto& fighter : fighters) {
        if (!fighter.is_player || fighter.side != "left") {
            continue;
        }
        Player& player = players_.at(fighter.player_name);
        const bool leveled = grant_player_reward(player, exp, gold, drops);
        out_ << "[reward] " << player.name << " receives " << exp << " exp and " << gold << " gold";
        if (leveled) {
            out_ << ". Level up to " << player.level << "!";
        }
        out_ << "\n";
        for (const auto& drop : drops) {
            out_ << "[reward] " << player.name << " receives item " << drop.first << " x"
                 << drop.second << "\n";
        }
        update_quest_progress(player, kills);
    }
}

bool GameServer::grant_player_reward(Player& player, int exp, int gold,
                                     const std::map<std::string, int>& items)
{
    player.exp += exp;
    player.gold += gold;
    for (const auto& item : items) {
        player.inventory[item.first] += item.second;
    }

    bool leveled = false;
    while (player.exp >= exp_to_next(player.level)) {
        player.exp -= exp_to_next(player.level);
        ++player.level;
        leveled = true;
    }
    return leveled;
}

void GameServer::update_quest_progress(Player& player, const std::map<std::string, int>& kills)
{
    for (auto& quest_pair : player.quests) {
        const auto def_it = config_.quests.find(quest_pair.first);
        if (def_it == config_.quests.end() || quest_pair.second.claimed) {
            continue;
        }
        const auto kill_it = kills.find(def_it->second.target_monster);
        if (kill_it == kills.end()) {
            continue;
        }
        const int before = quest_pair.second.progress;
        quest_pair.second.progress =
          std::min(def_it->second.required_kills, quest_pair.second.progress + kill_it->second);
        if (quest_pair.second.progress != before) {
            out_ << "[quest] " << player.name << " " << def_it->second.name << " progress "
                 << quest_pair.second.progress << "/" << def_it->second.required_kills << "\n";
        }
    }
}

int GameServer::exp_to_next(int level)
{
    return 40 + (level - 1) * 25;
}

Fighter GameServer::make_player_fighter(const Player& player, const std::string& side) const
{
    const int level_bonus = player.level - 1;
    return Fighter{ player.name,
                    player.name,
                    side,
                    player.name,
                    "",
                    true,
                    player.level,
                    120 + level_bonus * 12,
                    120 + level_bonus * 12,
                    30 + level_bonus * 3,
                    30 + level_bonus * 3,
                    18 + level_bonus * 2,
                    6 + level_bonus,
                    12,
                    0,
                    0,
                    false,
                    { "attack", "heavy", "defend", "heal", "fire" } };
}

Fighter GameServer::make_monster_fighter(const MonsterDef& def, int index, const std::string& side)
{
    return Fighter{ def.id + std::to_string(index),
                    def.name + "#" + std::to_string(index),
                    side,
                    "",
                    def.id,
                    false,
                    def.level,
                    def.max_hp,
                    def.max_hp,
                    def.max_mp,
                    def.max_mp,
                    def.attack,
                    def.defense,
                    def.speed,
                    def.exp,
                    def.gold,
                    false,
                    def.skills };
}

Player* GameServer::require_player(const std::string& name)
{
    if (name.empty()) {
        out_ << "[error] player name is required\n";
        return nullptr;
    }
    const auto it = players_.find(name);
    if (it == players_.end()) {
        out_ << "[error] unknown player: " << name << "\n";
        return nullptr;
    }
    return &it->second;
}

Battle* GameServer::active_battle(const std::string& id)
{
    const auto it = battles_.find(id);
    return it == battles_.end() ? nullptr : it->second.get();
}

void GameServer::remove_from_queue(const std::string& name)
{
    waiting_queue_.erase(std::remove(waiting_queue_.begin(), waiting_queue_.end(), name),
                         waiting_queue_.end());
    players_.at(name).queued = false;
}

std::string GameServer::pop_waiting_opponent(const std::string& self)
{
    while (!waiting_queue_.empty()) {
        const std::string candidate = waiting_queue_.front();
        waiting_queue_.erase(waiting_queue_.begin());
        auto it = players_.find(candidate);
        if (it != players_.end()) {
            it->second.queued = false;
            if (candidate != self && it->second.online && it->second.battle_id.empty()) {
                return candidate;
            }
        }
    }
    return "";
}

void GameServer::print_players() const
{
    if (players_.empty()) {
        out_ << "[server] no players\n";
        return;
    }
    for (const auto& pair : players_) {
        const Player& player = pair.second;
        out_ << "[player] " << player.name << " level=" << player.level << " exp=" << player.exp
             << "/" << exp_to_next(player.level) << " gold=" << player.gold
             << (player.online ? " online" : " offline");
        if (!player.battle_id.empty()) {
            out_ << " battle=" << player.battle_id;
        }
        if (player.queued) {
            out_ << " queued";
        }
        if (!player.inventory.empty()) {
            out_ << " inventory=" << inventory_summary(player);
        }
        if (!player.quests.empty()) {
            out_ << " quests=" << quest_summary(player);
        }
        out_ << "\n";
    }
}

void GameServer::give_item(const std::string& name, const std::string& item_id,
                           const std::string& amount_text)
{
    Player* player = require_player(name);
    if (player == nullptr) {
        return;
    }
    if (config_.items.find(item_id) == config_.items.end()) {
        out_ << "[error] unknown item: " << item_id << "\n";
        return;
    }
    int amount = 1;
    if (!amount_text.empty()) {
        amount = std::max(1, std::stoi(amount_text));
    }
    player->inventory[item_id] += amount;
    out_ << "[server] " << player->name << " receives " << item_id << " x" << amount << "\n";
}

void GameServer::print_inventory(const std::string& name) const
{
    const auto it = players_.find(name);
    if (name.empty()) {
        out_ << "[error] player name is required\n";
        return;
    }
    if (it == players_.end()) {
        out_ << "[error] unknown player: " << name << "\n";
        return;
    }
    out_ << "[inventory] " << name << ": " << inventory_summary(it->second) << "\n";
}

std::string GameServer::inventory_summary(const Player& player) const
{
    if (player.inventory.empty()) {
        return "empty";
    }

    std::string out;
    for (const auto& item : player.inventory) {
        if (!out.empty()) {
            out += ",";
        }
        out += item.first + "x" + std::to_string(item.second);
    }
    return out;
}

void GameServer::print_monsters() const
{
    for (const auto& pair : config_.monsters) {
        const MonsterDef& monster = pair.second;
        out_ << "[monster] " << monster.id << " name=" << monster.name << " level=" << monster.level
             << " hp=" << monster.max_hp << " reward=" << monster.exp << "exp/" << monster.gold
             << "gold"
             << " drops=" << drop_summary(monster) << "\n";
    }
    for (const auto& pair : config_.encounters) {
        const EncounterDef& encounter = pair.second;
        out_ << "[encounter] " << encounter.id << " name=\"" << encounter.name << "\""
             << " monsters=" << join_ids(encounter.monsters) << "\n";
    }
}

void GameServer::print_items() const
{
    for (const auto& pair : config_.items) {
        const ItemDef& item = pair.second;
        out_ << "[item] " << item.id << " name=" << item.name << " heal=" << item.heal
             << " price=" << item.price << " desc=\"" << item.description << "\"\n";
    }
}

void GameServer::print_shop() const
{
    for (const auto& pair : config_.items) {
        const ItemDef& item = pair.second;
        out_ << "[shop] " << item.id << " name=" << item.name << " price=" << item.price
             << " heal=" << item.heal << "\n";
    }
}

void GameServer::buy_item(const std::string& name, const std::string& item_id,
                          const std::string& amount_text)
{
    Player* player = require_player(name);
    if (player == nullptr) {
        return;
    }
    const auto item_it = config_.items.find(item_id);
    if (item_it == config_.items.end()) {
        out_ << "[error] unknown item: " << item_id << "\n";
        return;
    }

    int amount = 1;
    if (!amount_text.empty()) {
        amount = std::max(1, std::stoi(amount_text));
    }

    const int cost = item_it->second.price * amount;
    if (player->gold < cost) {
        out_ << "[error] " << name << " lacks gold. need=" << cost << " have=" << player->gold
             << "\n";
        return;
    }

    player->gold -= cost;
    player->inventory[item_id] += amount;
    out_ << "[shop] " << name << " bought " << item_id << " x" << amount << " for " << cost
         << " gold\n";
}

void GameServer::print_quest_defs() const
{
    for (const auto& pair : config_.quests) {
        const QuestDef& quest = pair.second;
        out_ << "[quest] " << quest.id << " name=\"" << quest.name << "\""
             << " target=" << quest.target_monster << " kills=" << quest.required_kills
             << " reward=" << quest.reward_exp << "exp/" << quest.reward_gold << "gold"
             << " items=" << item_map_summary(quest.reward_items) << "\n";
    }
}

void GameServer::handle_quest_command(const std::string& subcmd, const std::string& name,
                                      const std::string& quest_id)
{
    if (subcmd == "accept") {
        accept_quest(name, quest_id);
    }
    else if (subcmd == "list") {
        print_player_quests(name);
    }
    else if (subcmd == "claim") {
        claim_quest(name, quest_id);
    }
    else {
        out_ << "[error] quest command: accept/list/claim\n";
    }
}

void GameServer::accept_quest(const std::string& name, const std::string& quest_id)
{
    Player* player = require_player(name);
    if (player == nullptr) {
        return;
    }
    const auto quest_it = config_.quests.find(quest_id);
    if (quest_it == config_.quests.end()) {
        out_ << "[error] unknown quest: " << quest_id << "\n";
        return;
    }
    if (player->quests.find(quest_id) != player->quests.end()) {
        out_ << "[quest] " << name << " already has " << quest_id << "\n";
        return;
    }
    player->quests[quest_id] = QuestState{};
    out_ << "[quest] " << name << " accepted " << quest_it->second.name << "\n";
}

void GameServer::print_player_quests(const std::string& name) const
{
    const auto player_it = players_.find(name);
    if (name.empty()) {
        out_ << "[error] player name is required\n";
        return;
    }
    if (player_it == players_.end()) {
        out_ << "[error] unknown player: " << name << "\n";
        return;
    }
    const Player& player = player_it->second;
    if (player.quests.empty()) {
        out_ << "[quest] " << name << " has no quests\n";
        return;
    }
    for (const auto& pair : player.quests) {
        const auto def_it = config_.quests.find(pair.first);
        if (def_it == config_.quests.end()) {
            continue;
        }
        const QuestDef& quest = def_it->second;
        out_ << "[quest] " << pair.first << " " << quest.name << " " << pair.second.progress << "/"
             << quest.required_kills << (pair.second.claimed ? " claimed" : " active") << "\n";
    }
}

void GameServer::claim_quest(const std::string& name, const std::string& quest_id)
{
    Player* player = require_player(name);
    if (player == nullptr) {
        return;
    }
    auto state_it = player->quests.find(quest_id);
    if (state_it == player->quests.end()) {
        out_ << "[error] " << name << " has not accepted " << quest_id << "\n";
        return;
    }
    const auto quest_it = config_.quests.find(quest_id);
    if (quest_it == config_.quests.end()) {
        out_ << "[error] unknown quest: " << quest_id << "\n";
        return;
    }
    QuestState& state = state_it->second;
    const QuestDef& quest = quest_it->second;
    if (state.claimed) {
        out_ << "[quest] " << name << " already claimed " << quest_id << "\n";
        return;
    }
    if (state.progress < quest.required_kills) {
        out_ << "[quest] " << name << " progress " << state.progress << "/" << quest.required_kills
             << "\n";
        return;
    }

    const bool leveled =
      grant_player_reward(*player, quest.reward_exp, quest.reward_gold, quest.reward_items);
    state.claimed = true;
    out_ << "[quest] " << name << " claimed " << quest.name << " reward " << quest.reward_exp
         << " exp " << quest.reward_gold << " gold";
    if (!quest.reward_items.empty()) {
        out_ << " items=" << item_map_summary(quest.reward_items);
    }
    if (leveled) {
        out_ << " level=" << player->level;
    }
    out_ << "\n";
}

std::string GameServer::quest_summary(const Player& player) const
{
    std::string out;
    for (const auto& pair : player.quests) {
        const auto quest_it = config_.quests.find(pair.first);
        if (quest_it == config_.quests.end()) {
            continue;
        }
        if (!out.empty()) {
            out += ",";
        }
        out += pair.first + ":" + std::to_string(pair.second.progress) + "/"
               + std::to_string(quest_it->second.required_kills);
        if (pair.second.claimed) {
            out += ":claimed";
        }
    }
    return out.empty() ? "none" : out;
}

std::string GameServer::item_map_summary(const std::map<std::string, int>& items)
{
    if (items.empty()) {
        return "none";
    }
    std::string out;
    for (const auto& item : items) {
        if (!out.empty()) {
            out += ",";
        }
        out += item.first + "x" + std::to_string(item.second);
    }
    return out;
}

std::string GameServer::drop_summary(const MonsterDef& monster)
{
    if (monster.drops.empty()) {
        return "none";
    }
    std::string out;
    for (const auto& drop : monster.drops) {
        if (!out.empty()) {
            out += ",";
        }
        out += drop.first + "x" + std::to_string(drop.second);
    }
    return out;
}

std::string GameServer::join_ids(const std::vector<std::string>& values)
{
    if (values.empty()) {
        return "none";
    }

    std::string out;
    for (const auto& value : values) {
        if (!out.empty()) {
            out += ",";
        }
        out += value;
    }
    return out;
}

void GameServer::print_skills() const
{
    for (const auto& pair : config_.skills) {
        const SkillDef& skill = pair.second;
        out_ << "[skill] " << skill.id << " name=" << skill.name << " mp=" << skill.mp_cost << "\n";
    }
}

void GameServer::print_state() const
{
    print_players();
    out_ << "[server] queue:";
    for (const auto& name : waiting_queue_) {
        out_ << " " << name;
    }
    out_ << "\n";

    out_ << "[server] active battles:";
    for (const auto& pair : battles_) {
        out_ << " " << pair.first;
    }
    out_ << "\n";
    for (const auto& pair : battles_) {
        pair.second->print_state();
    }
}

void GameServer::print_log(const std::string& id) const
{
    std::string battle_id = id;
    if (battle_id.empty()) {
        battle_id = "B" + std::to_string(next_battle_id_ - 1);
    }

    auto active = battles_.find(battle_id);
    if (active != battles_.end()) {
        active->second->print_log();
        return;
    }

    auto historical = history_.find(battle_id);
    if (historical != history_.end()) {
        historical->second->print_log();
        return;
    }

    out_ << "[error] unknown battle: " << battle_id << "\n";
}

void GameServer::save_players() const
{
    const std::filesystem::path path(player_file_);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream file(player_file_, std::ios::trunc);
    if (!file) {
        out_ << "[error] cannot open " << player_file_ << "\n";
        return;
    }

    nlohmann::json root;
    root["version"] = 1;
    root["players"] = nlohmann::json::array();
    for (const auto& pair : players_) {
        const Player& player = pair.second;
        nlohmann::json player_json;
        player_json["name"] = player.name;
        player_json["level"] = player.level;
        player_json["exp"] = player.exp;
        player_json["gold"] = player.gold;
        player_json["inventory"] = player.inventory;

        nlohmann::json quests = nlohmann::json::object();
        for (const auto& quest : player.quests) {
            quests[quest.first] = {
                { "progress", quest.second.progress },
                { "claimed", quest.second.claimed },
            };
        }
        player_json["quests"] = std::move(quests);
        root["players"].push_back(std::move(player_json));
    }

    file << root.dump(2) << '\n';
    out_ << "[server] saved " << players_.size() << " players to " << player_file_ << "\n";
}
void GameServer::load_players()
{
    std::ifstream file(player_file_);
    if (!file) {
        out_ << "[error] cannot open " << player_file_ << "\n";
        return;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(file);
    }
    catch (const nlohmann::json::exception&) {
        out_ << "[error] invalid player json: " << player_file_ << "\n";
        return;
    }

    players_.clear();
    waiting_queue_.clear();
    battles_.clear();
    history_.clear();

    const nlohmann::json saved_players = root.value("players", nlohmann::json::array());
    for (const auto& item : saved_players) {
        Player player;
        player.name = item.value("name", "");
        if (player.name.empty()) {
            continue;
        }
        player.level = item.value("level", 1);
        player.exp = item.value("exp", 0);
        player.gold = item.value("gold", 0);
        player.online = false;

        const nlohmann::json inventory = item.value("inventory", nlohmann::json::object());
        if (inventory.is_object()) {
            for (const auto& inv : inventory.items()) {
                const int count = inv.value().get<int>();
                if (count > 0) {
                    player.inventory[inv.key()] = count;
                }
            }
        }

        const nlohmann::json quests = item.value("quests", nlohmann::json::object());
        if (quests.is_object()) {
            for (const auto& quest : quests.items()) {
                QuestState state;
                state.progress = quest.value().value("progress", 0);
                state.claimed = quest.value().value("claimed", false);
                player.quests[quest.key()] = state;
            }
        }

        players_[player.name] = std::move(player);
    }

    out_ << "[server] loaded " << players_.size() << " players from " << player_file_ << "\n";
}
void GameServer::print_help() const
{
    out_ << "commands:\n"
         << "  login <name>             create a player session\n"
         << "  logout <name>            mark player offline when not in battle\n"
         << "  forfeit <name>           concede current battle\n"
         << "  pve <name> [encounter]   start PVE: slime / fox / bandit / forest / camp\n"
         << "  queue <name>             enter 1v1 matchmaking\n"
         << "  attack <name> [target]   normal attack\n"
         << "  heavy <name> [target]    heavy strike, costs 8 MP\n"
         << "  skill <name> [target]    alias of heavy\n"
         << "  fire <name> [target]     fire charm, costs 10 MP\n"
         << "  defend <name>            reduce incoming damage this round\n"
         << "  heal <name> [target]     heal self or ally, costs 6 MP\n"
         << "  use <name> <item> [target] use a battle item, e.g. use alice potion\n"
         << "  inventory <name>         list player inventory\n"
         << "  give <name> <item> [n]   debug grant item\n"
         << "  shop                     list shop items\n"
         << "  buy <name> <item> [n]    buy items with gold\n"
         << "  quests                   list quest templates\n"
         << "  quest accept <name> <id> accept quest\n"
         << "  quest list <name>        list player quests\n"
         << "  quest claim <name> <id>  claim completed quest\n"
         << "  players                  list player profiles\n"
         << "  monsters                 list monster templates\n"
         << "  items                    list item templates\n"
         << "  skills                   list skill templates\n"
         << "  state                    print server state\n"
         << "  log [battle_id]          print latest or selected battle log\n"
         << "  save                     save players to data/players.json\n"
         << "  load                     load players from data/players.json\n"
         << "  help                     show commands\n"
         << "  quit                     exit\n";
}

}  // namespace mm
