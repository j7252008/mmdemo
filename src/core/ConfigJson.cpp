#include "Types.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace mm {
namespace {

using json = nlohmann::json;

SkillKind parse_skill_kind(const std::string& text)
{
    if (text == "heal") {
        return SkillKind::Heal;
    }
    if (text == "defend") {
        return SkillKind::Defend;
    }
    return SkillKind::Damage;
}

TargetRule parse_target_rule(const std::string& text)
{
    if (text == "ally") {
        return TargetRule::Ally;
    }
    if (text == "self") {
        return TargetRule::Self;
    }
    return TargetRule::Enemy;
}

std::map<std::string, int> read_int_map(const json& object)
{
    std::map<std::string, int> values;
    if (!object.is_object()) {
        return values;
    }
    for (const auto& pair : object.items()) {
        const int amount = pair.value().get<int>();
        if (amount > 0) {
            values[pair.key()] = amount;
        }
    }
    return values;
}

std::vector<std::string> read_string_vector(const json& array)
{
    std::vector<std::string> values;
    if (!array.is_array()) {
        return values;
    }
    for (const auto& value : array) {
        values.push_back(value.get<std::string>());
    }
    return values;
}

void load_skills(Config& cfg, const json& root)
{
    if (!root.contains("skills")) {
        return;
    }

    std::map<std::string, SkillDef> skills;
    for (const auto& item : root.at("skills")) {
        SkillDef def;
        def.key = item.at("id").get<std::string>();
        def.name = item.value("name", def.key);
        def.kind = parse_skill_kind(item.value("kind", "damage"));
        def.target = parse_target_rule(item.value("target", "enemy"));
        def.mp_cost = item.value("mp_cost", 0);
        def.power = item.value("power", 0);
        def.min_heal = item.value("min_heal", 0);
        def.max_heal = item.value("max_heal", 0);
        skills.emplace(def.key, std::move(def));
    }
    if (skills.find("attack") == skills.end()) {
        skills.emplace("attack", SkillDef{ "attack", "Attack", SkillKind::Damage,
                                           TargetRule::Enemy, 0, 0, 0, 0 });
    }
    cfg.skills = std::move(skills);
}

void load_items(Config& cfg, const json& root)
{
    if (!root.contains("items")) {
        return;
    }

    std::map<std::string, ItemDef> items;
    for (const auto& item : root.at("items")) {
        ItemDef def;
        def.key = item.at("id").get<std::string>();
        def.name = item.value("name", def.key);
        def.heal = item.value("heal", 0);
        def.price = item.value("price", 0);
        def.description = item.value("description", "");
        items.emplace(def.key, std::move(def));
    }
    cfg.items = std::move(items);
}

void load_monsters(Config& cfg, const json& root)
{
    if (!root.contains("monsters")) {
        return;
    }

    std::map<std::string, MonsterDef> monsters;
    for (const auto& item : root.at("monsters")) {
        MonsterDef def;
        def.key = item.at("id").get<std::string>();
        def.name = item.value("name", def.key);
        def.level = item.value("level", 1);
        def.max_hp = item.value("max_hp", 1);
        def.max_mp = item.value("max_mp", 0);
        def.attack = item.value("attack", 1);
        def.defense = item.value("defense", 0);
        def.speed = item.value("speed", 1);
        def.exp = item.value("exp", 0);
        def.gold = item.value("gold", 0);
        def.drops = read_int_map(item.value("drops", json::object()));
        def.skills = read_string_vector(item.value("skills", json::array({ "attack" })));
        monsters.emplace(def.key, std::move(def));
    }
    cfg.monsters = std::move(monsters);
}

void load_quests(Config& cfg, const json& root)
{
    if (!root.contains("quests")) {
        return;
    }

    std::map<std::string, QuestDef> quests;
    for (const auto& item : root.at("quests")) {
        QuestDef def;
        def.key = item.at("id").get<std::string>();
        def.name = item.value("name", def.key);
        def.target_monster = item.value("target_monster", "");
        def.required_kills = item.value("required_kills", 0);
        def.reward_exp = item.value("reward_exp", 0);
        def.reward_gold = item.value("reward_gold", 0);
        def.reward_items = read_int_map(item.value("reward_items", json::object()));
        quests.emplace(def.key, std::move(def));
    }
    cfg.quests = std::move(quests);
}

void load_encounters(Config& cfg, const json& root)
{
    if (!root.contains("encounters")) {
        return;
    }

    std::map<std::string, EncounterDef> encounters;
    for (const auto& item : root.at("encounters")) {
        EncounterDef def;
        def.key = item.at("id").get<std::string>();
        def.name = item.value("name", def.key);
        def.monsters = read_string_vector(item.value("monsters", json::array()));
        encounters.emplace(def.key, std::move(def));
    }
    cfg.encounters = std::move(encounters);
}

}  // namespace

Config Config::load_json_or_default(const std::string& path)
{
    std::ifstream file(path);
    if (!file) {
        return Config::make_default();
    }

    try {
        Config cfg = Config::make_default();
        const json root = json::parse(file);
        load_skills(cfg, root);
        load_items(cfg, root);
        load_monsters(cfg, root);
        load_quests(cfg, root);
        load_encounters(cfg, root);
        return cfg;
    }
    catch (const json::exception&) {
        return Config::make_default();
    }
}

}  // namespace mm
