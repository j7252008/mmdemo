#pragma once

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace mm {

// Static skill behavior loaded into Config. Battle actions reference skills by command key.
enum class SkillKind {
    Damage,
    Heal,
    Defend,
};

enum class TargetRule {
    Enemy,
    Ally,
    Self,
};

enum class BattleMode {
    Pve,
    Pvp,
};

enum class FighterSide {
    Left,
    Right,
};

// Template data: these definitions are shared by all players and battles.
struct SkillDef
{
    std::string key;
    std::string name;
    SkillKind kind = SkillKind::Damage;
    TargetRule target = TargetRule::Enemy;
    int mp_cost = 0;
    int power = 0;
    int min_heal = 0;
    int max_heal = 0;
};

// Monster templates double as reward tables for PVE settlement.
struct MonsterDef
{
    std::string key;
    std::string name;
    int level = 1;
    int max_hp = 1;
    int max_mp = 0;
    int attack = 1;
    int defense = 0;
    int speed = 1;
    int exp = 0;
    int gold = 0;
    std::map<std::string, int> drops;
    std::vector<std::string> skills;
};

// Item templates are intentionally small for the text prototype: only battle healing and shop
// price.
struct ItemDef
{
    std::string key;
    std::string name;
    int heal = 0;
    int price = 0;
    std::string description;
};

// Quest templates currently track monster kills and pay out exp/gold/items.
struct QuestDef
{
    std::string key;
    std::string name;
    std::string target_monster;
    int required_kills = 0;
    int reward_exp = 0;
    int reward_gold = 0;
    std::map<std::string, int> reward_items;
};

// Per-player quest progress. QuestDef stays immutable; this state is saved with the player.
struct QuestState
{
    int progress = 0;
    bool claimed = false;
};

// PVE entry point. Single-monster encounters keep old commands compatible; groups model dungeons.
struct EncounterDef
{
    std::string key;
    std::string name;
    std::vector<std::string> monsters;
};

// Persistent player profile plus volatile online/matchmaking/battle placement.
struct Player
{
    std::string name;
    int level = 1;
    int exp = 0;
    int gold = 0;
    bool online = true;
    bool queued = false;
    int battle_id = 0;
    std::map<std::string, int> inventory;
    std::map<std::string, QuestState> quests;
};

// Runtime combatant snapshot. Players and monsters are normalized into fighters inside a Battle.
struct Fighter
{
    int id = 0;
    std::string name;
    std::string command_key;
    FighterSide side = FighterSide::Left;
    std::string player_name;
    std::string monster_key;
    bool is_player = false;
    int level = 1;
    int hp = 1;
    int max_hp = 1;
    int mp = 0;
    int max_mp = 0;
    int attack = 1;
    int defense = 0;
    int speed = 1;
    int exp = 0;
    int gold = 0;
    bool defending = false;
    std::vector<std::string> skills;

    bool alive() const { return hp > 0; }
};

// A locked-in action for the current round. Player actions and monster AI share this form.
struct Action
{
    int actor_id = 0;
    std::string skill_key;
    int target_id = 0;
    std::string item_key;
    bool use_item = false;
};

// Battle log event. Text is deliberately client-friendly for the current no-assets prototype.
struct Event
{
    int round = 0;
    std::string kind;
    std::string text;
};

// In-memory data table. This can later be loaded from Lua/JSON without changing Battle rules.
struct Config
{
    std::map<std::string, SkillDef> skills;
    std::map<std::string, MonsterDef> monsters;
    std::map<std::string, ItemDef> items;
    std::map<std::string, QuestDef> quests;
    std::map<std::string, EncounterDef> encounters;

    static Config load_json_or_default(const std::string& path);

    static Config make_default()
    {
        Config cfg;
        cfg.skills.emplace("attack", SkillDef{ "attack", "Attack", SkillKind::Damage,
                                               TargetRule::Enemy, 0, 0, 0, 0 });
        cfg.skills.emplace("heavy", SkillDef{ "heavy", "Heavy Strike", SkillKind::Damage,
                                              TargetRule::Enemy, 8, 14, 0, 0 });
        cfg.skills.emplace("fire", SkillDef{ "fire", "Fire Charm", SkillKind::Damage,
                                             TargetRule::Enemy, 10, 20, 0, 0 });
        cfg.skills.emplace("defend", SkillDef{ "defend", "Defend", SkillKind::Defend,
                                               TargetRule::Self, 0, 0, 0, 0 });
        cfg.skills.emplace(
          "heal", SkillDef{ "heal", "Heal", SkillKind::Heal, TargetRule::Ally, 6, 0, 18, 28 });

        cfg.items.emplace("potion",
                          ItemDef{ "potion", "Small Potion", 35, 10, "Restores 35 HP in battle." });
        cfg.items.emplace("hi_potion",
                          ItemDef{ "hi_potion", "Hi-Potion", 70, 24, "Restores 70 HP in battle." });

        cfg.monsters.emplace("slime", MonsterDef{ "slime",
                                                  "Green Slime",
                                                  1,
                                                  75,
                                                  10,
                                                  13,
                                                  3,
                                                  7,
                                                  12,
                                                  8,
                                                  { { "potion", 1 } },
                                                  { "attack" } });
        cfg.monsters.emplace("fox", MonsterDef{ "fox",
                                                "Mountain Fox",
                                                2,
                                                92,
                                                18,
                                                16,
                                                4,
                                                13,
                                                18,
                                                12,
                                                { { "potion", 2 } },
                                                { "attack", "heavy" } });
        cfg.monsters.emplace("bandit", MonsterDef{ "bandit",
                                                   "Wild Bandit",
                                                   3,
                                                   135,
                                                   26,
                                                   20,
                                                   7,
                                                   10,
                                                   30,
                                                   22,
                                                   { { "hi_potion", 1 } },
                                                   { "attack", "heavy", "heal" } });

        cfg.quests.emplace(
          "slime_hunter",
          QuestDef{ "slime_hunter", "Slime Hunter", "slime", 2, 20, 15, { { "potion", 1 } } });
        cfg.quests.emplace(
          "fox_trouble",
          QuestDef{ "fox_trouble", "Fox Trouble", "fox", 1, 28, 20, { { "hi_potion", 1 } } });

        cfg.encounters.emplace("slime", EncounterDef{ "slime", "Single Slime", { "slime" } });
        cfg.encounters.emplace("fox", EncounterDef{ "fox", "Single Fox", { "fox" } });
        cfg.encounters.emplace("bandit", EncounterDef{ "bandit", "Single Bandit", { "bandit" } });
        cfg.encounters.emplace("forest",
                               EncounterDef{ "forest", "Forest Patrol", { "slime", "fox" } });
        cfg.encounters.emplace("camp", EncounterDef{ "camp", "Bandit Camp", { "bandit", "fox" } });
        return cfg;
    }
};

inline int clamp_int(int value, int low, int high)
{
    return std::max(low, std::min(high, value));
}

inline std::vector<std::string> split_words(const std::string& line)
{
    std::istringstream input(line);
    std::vector<std::string> out;
    std::string word;
    while (input >> word) {
        out.push_back(word);
    }
    return out;
}

inline bool contains(const std::vector<std::string>& values, const std::string& needle)
{
    return std::find(values.begin(), values.end(), needle) != values.end();
}

}  // namespace mm
