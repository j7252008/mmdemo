#include "Battle.h"

#include <algorithm>
#include <utility>

namespace mm {

Battle::Battle(std::string id, std::string mode, std::vector<Fighter> fighters,
               const Config& config, std::mt19937& rng, OutputSink& out)
  : id_(std::move(id))
  , mode_(std::move(mode))
  , fighters_(std::move(fighters))
  , config_(config)
  , rng_(rng)
  , out_(out)
{}

const std::string& Battle::id() const
{
    return id_;
}

const std::vector<Event>& Battle::events() const
{
    return events_;
}

const std::vector<Fighter>& Battle::fighters() const
{
    return fighters_;
}

bool Battle::closed() const
{
    return closed_;
}

void Battle::start()
{
    add_event("start",
              "Battle " + id_ + " starts: " + side_names("left") + " vs " + side_names("right"));
    begin_round();
}

bool Battle::submit_action(const std::string& player_name, const std::string& skill_id,
                           const std::string& target_id, std::string& error)
{
    if (closed_) {
        error = "battle is closed";
        return false;
    }

    Fighter* actor = fighter_for_player(player_name);
    if (actor == nullptr || !actor->alive()) {
        error = "player cannot act";
        return false;
    }

    auto skill_it = config_.skills.find(skill_id);
    if (skill_it == config_.skills.end()) {
        error = "unknown skill";
        return false;
    }
    if (!contains(actor->skills, skill_id)) {
        error = "fighter cannot use skill";
        return false;
    }

    Fighter* target = resolve_target(*actor, skill_it->second, target_id);
    if (target == nullptr) {
        error = "no valid target";
        return false;
    }

    pending_actions_[actor->id] = Action{ actor->id, skill_id, target->id };
    out_ << "[server] action locked: " << player_name << " -> " << skill_it->second.name << " / "
         << target->name << "\n";

    if (all_ready()) {
        resolve_ready_round();
    }
    return true;
}

bool Battle::submit_item_action(const std::string& player_name, const std::string& item_id,
                                const std::string& target_id, std::string& error)
{
    if (closed_) {
        error = "battle is closed";
        return false;
    }

    Fighter* actor = fighter_for_player(player_name);
    if (actor == nullptr || !actor->alive()) {
        error = "player cannot act";
        return false;
    }

    const auto item_it = config_.items.find(item_id);
    if (item_it == config_.items.end()) {
        error = "unknown item";
        return false;
    }

    Fighter* target = target_id.empty() ? actor : fighter_by_id(target_id);
    if (target == nullptr || !target->alive() || target->side != actor->side) {
        error = "no valid item target";
        return false;
    }

    pending_actions_[actor->id] = Action{ actor->id, "", target->id, item_id, true };
    out_ << "[server] action locked: " << player_name << " -> use " << item_it->second.name << " / "
         << target->name << "\n";

    if (all_ready()) {
        resolve_ready_round();
    }
    return true;
}

bool Battle::forfeit_player(const std::string& player_name, std::string& error)
{
    if (closed_) {
        error = "battle is closed";
        return false;
    }

    Fighter* fighter = fighter_for_player(player_name);
    if (fighter == nullptr) {
        error = "player is not in this battle";
        return false;
    }
    if (!fighter->alive()) {
        error = "player is already defeated";
        return false;
    }

    fighter->hp = 0;
    pending_actions_.erase(fighter->id);
    add_event("forfeit", fighter->name + " forfeits the battle.");
    finish();
    return true;
}

void Battle::print_state() const
{
    out_ << "[state]";
    for (const auto& fighter : fighters_) {
        out_ << " " << fighter.id << "=" << fighter.name << " HP " << fighter.hp << "/"
             << fighter.max_hp << " MP " << fighter.mp << "/" << fighter.max_mp;
    }
    out_ << "\n";
}

void Battle::print_log() const
{
    for (const auto& event : events_) {
        out_ << "[log][" << event.round << "][" << event.kind << "] " << event.text << "\n";
    }
}

std::vector<std::string> Battle::player_names() const
{
    std::vector<std::string> names;
    for (const auto& fighter : fighters_) {
        if (fighter.is_player) {
            names.push_back(fighter.player_name);
        }
    }
    return names;
}

std::string Battle::mode() const
{
    return mode_;
}

void Battle::begin_round()
{
    ++round_;
    pending_actions_.clear();
    for (auto& fighter : fighters_) {
        fighter.defending = false;
    }

    add_event("round_start", "Round " + std::to_string(round_) + " begins.");
    submit_ai_actions();
    print_state();

    if (all_ready()) {
        resolve_ready_round();
    }
}

void Battle::submit_ai_actions()
{
    for (auto& fighter : fighters_) {
        if (!fighter.is_player && fighter.alive()) {
            const std::string skill_id = choose_ai_skill(fighter);
            const SkillDef& skill = config_.skills.at(skill_id);
            Fighter* target = resolve_target(fighter, skill, "");
            if (target != nullptr) {
                pending_actions_[fighter.id] = Action{ fighter.id, skill_id, target->id };
            }
        }
    }
}

std::string Battle::choose_ai_skill(const Fighter& fighter)
{
    const auto heal_it = config_.skills.find("heal");
    if (heal_it != config_.skills.end() && contains(fighter.skills, "heal")
        && fighter.hp * 100 <= fighter.max_hp * 35 && fighter.mp >= heal_it->second.mp_cost) {
        return "heal";
    }

    std::vector<std::string> available;
    for (const auto& skill_id : fighter.skills) {
        const auto skill_it = config_.skills.find(skill_id);
        if (skill_it != config_.skills.end() && fighter.mp >= skill_it->second.mp_cost
            && skill_it->second.kind != SkillKind::Defend) {
            available.push_back(skill_id);
        }
    }

    if (available.empty()) {
        return "attack";
    }

    std::uniform_int_distribution<size_t> dist(0, available.size() - 1);
    return available[dist(rng_)];
}

void Battle::resolve_ready_round()
{
    resolve_round();
    print_state();

    if (!winner_side().has_value()) {
        begin_round();
    }
    else {
        finish();
    }
}

void Battle::resolve_round()
{
    std::vector<Fighter*> order;
    for (auto& fighter : fighters_) {
        if (fighter.alive()) {
            order.push_back(&fighter);
        }
    }

    std::sort(order.begin(), order.end(), [](const Fighter* a, const Fighter* b) {
        if (a->speed == b->speed) {
            return a->id < b->id;
        }
        return a->speed > b->speed;
    });

    for (Fighter* actor : order) {
        if (!actor->alive()) {
            continue;
        }

        const auto action_it = pending_actions_.find(actor->id);
        if (action_it == pending_actions_.end()) {
            continue;
        }

        if (action_it->second.use_item) {
            Fighter* target = fighter_by_id(action_it->second.target_id);
            const auto item_it = config_.items.find(action_it->second.item_id);
            if (target != nullptr && target->alive() && item_it != config_.items.end()) {
                resolve_item_action(*actor, *target, item_it->second);
            }
        }
        else {
            const SkillDef& skill = config_.skills.at(action_it->second.skill_id);
            Fighter* target = fighter_by_id(action_it->second.target_id);
            if (target == nullptr || !target->alive()) {
                target = resolve_target(*actor, skill, "");
            }
            if (target != nullptr) {
                resolve_action(*actor, *target, skill);
            }
        }
        if (winner_side().has_value()) {
            break;
        }
    }
}

void Battle::resolve_item_action(Fighter& actor, Fighter& target, const ItemDef& item)
{
    const int before = target.hp;
    target.hp = clamp_int(target.hp + item.heal, 0, target.max_hp);
    add_event("item", actor.name + " uses " + item.name + " on " + target.name + ": +"
                        + std::to_string(target.hp - before) + " HP.");
}

void Battle::resolve_action(Fighter& actor, Fighter& target, const SkillDef& requested_skill)
{
    const SkillDef* skill = &requested_skill;
    Fighter* resolved_target = &target;

    if (actor.mp < skill->mp_cost) {
        add_event("fail", actor.name + " tried " + skill->name + " but lacked MP, using Attack.");
        skill = &config_.skills.at("attack");
        resolved_target = resolve_target(actor, *skill, "");
        if (resolved_target == nullptr) {
            return;
        }
    }

    if (skill->kind == SkillKind::Defend) {
        actor.defending = true;
        add_event("defend", actor.name + " braces for impact.");
        return;
    }

    actor.mp -= skill->mp_cost;

    if (skill->kind == SkillKind::Heal) {
        std::uniform_int_distribution<int> dist(skill->min_heal, skill->max_heal);
        const int amount = dist(rng_) + actor.level * 2;
        const int before = resolved_target->hp;
        resolved_target->hp = clamp_int(resolved_target->hp + amount, 0, resolved_target->max_hp);
        add_event("heal", actor.name + " uses " + skill->name + " on " + resolved_target->name
                            + ": +" + std::to_string(resolved_target->hp - before) + " HP.");
        return;
    }

    std::uniform_int_distribution<int> dist(0, 8);
    const int base = actor.attack + skill->power + dist(rng_);
    int damage = std::max(1, base - resolved_target->defense);
    if (resolved_target->defending) {
        damage = std::max(1, damage / 2);
    }

    resolved_target->hp = clamp_int(resolved_target->hp - damage, 0, resolved_target->max_hp);
    add_event("damage", actor.name + " uses " + skill->name + " on " + resolved_target->name + ": "
                          + std::to_string(damage) + " damage.");

    if (!resolved_target->alive()) {
        add_event("death", resolved_target->name + " falls.");
    }
}

bool Battle::all_ready() const
{
    for (const auto& fighter : fighters_) {
        if (fighter.is_player && fighter.alive()
            && pending_actions_.find(fighter.id) == pending_actions_.end()) {
            return false;
        }
    }
    return true;
}

Fighter* Battle::resolve_target(const Fighter& actor, const SkillDef& skill,
                                const std::string& requested_id)
{
    if (skill.target == TargetRule::Self) {
        return fighter_by_id(actor.id);
    }

    if (!requested_id.empty()) {
        Fighter* requested = fighter_by_id(requested_id);
        if (requested != nullptr && requested->alive()) {
            if (skill.target == TargetRule::Enemy && requested->side != actor.side) {
                return requested;
            }
            if (skill.target == TargetRule::Ally && requested->side == actor.side) {
                return requested;
            }
        }
    }

    std::vector<Fighter*> candidates;
    for (auto& fighter : fighters_) {
        if (!fighter.alive()) {
            continue;
        }
        if (skill.target == TargetRule::Enemy && fighter.side != actor.side) {
            candidates.push_back(&fighter);
        }
        else if (skill.target == TargetRule::Ally && fighter.side == actor.side) {
            candidates.push_back(&fighter);
        }
    }

    if (candidates.empty()) {
        return nullptr;
    }

    std::sort(candidates.begin(), candidates.end(), [](const Fighter* a, const Fighter* b) {
        if (a->hp == b->hp) {
            return a->id < b->id;
        }
        return a->hp < b->hp;
    });
    return candidates.front();
}

Fighter* Battle::fighter_for_player(const std::string& player_name)
{
    for (auto& fighter : fighters_) {
        if (fighter.is_player && fighter.player_name == player_name) {
            return &fighter;
        }
    }
    return nullptr;
}

Fighter* Battle::fighter_by_id(const std::string& id)
{
    for (auto& fighter : fighters_) {
        if (fighter.id == id || fighter.name == id || fighter.player_name == id) {
            return &fighter;
        }
    }
    return nullptr;
}

std::string Battle::side_names(const std::string& side) const
{
    std::string out;
    for (const auto& fighter : fighters_) {
        if (fighter.side == side) {
            if (!out.empty()) {
                out += ", ";
            }
            out += fighter.name;
        }
    }
    return out;
}

std::optional<std::string> Battle::winner_side() const
{
    bool left_alive = false;
    bool right_alive = false;
    for (const auto& fighter : fighters_) {
        if (!fighter.alive()) {
            continue;
        }
        if (fighter.side == "left") {
            left_alive = true;
        }
        else {
            right_alive = true;
        }
    }

    if (left_alive && !right_alive) {
        return "left";
    }
    if (right_alive && !left_alive) {
        return "right";
    }
    return std::nullopt;
}

void Battle::finish()
{
    const auto winner = winner_side();
    if (winner.has_value()) {
        add_event("end", side_names(*winner) + " wins.");
    }
    closed_ = true;
}

void Battle::add_event(std::string kind, std::string text)
{
    events_.push_back(Event{ round_, std::move(kind), text });
    out_ << "[battle] " << text << "\n";
}

}  // namespace mm
