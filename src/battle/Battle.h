#pragma once

#include "../core/Output.h"
#include "../core/Types.h"

#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace mm {

// Authoritative turn-based battle room. It owns round state, AI choices, action validation,
// target resolution, and battle logs; rewards are settled by GameServer after the room closes.
class Battle
{
public:
    Battle(int id, BattleMode mode, std::vector<Fighter> fighters, const Config& config,
           std::mt19937& rng, OutputSink& out);

    int id() const;
    const std::vector<Event>& events() const;
    const std::vector<Fighter>& fighters() const;
    bool closed() const;

    void start();

    // Player skill input is locked for the current round. When all living players have acted,
    // the room resolves every pending action in speed order.
    bool submit_action(const std::string& player_name, const std::string& skill_key,
                       const std::string& target_text, std::string& error);

    // Battle item usage follows the same pending-action path as skills so turn order stays uniform.
    bool submit_item_action(const std::string& player_name, const std::string& item_key,
                            const std::string& target_text, std::string& error);

    // Forfeit is an immediate terminal action. GameServer will clean player battle bindings
    // afterward.
    bool forfeit_player(const std::string& player_name, std::string& error);

    void print_state() const;
    void print_log() const;
    std::vector<std::string> player_names() const;
    BattleMode mode() const;

private:
    // A round starts by clearing transient defense/action state, scheduling monster AI, and
    // waiting for player actions. Pure PVE can resolve immediately after the player acts.
    void begin_round();

    // Monster AI is intentionally simple: heal at low HP if possible, otherwise choose an
    // affordable attack.
    void submit_ai_actions();
    std::string choose_ai_skill(const Fighter& fighter);

    // Resolve all ready actions once, then either begin the next round or close the battle.
    void resolve_ready_round();

    // Speed order is the only initiative rule for now; ties are stable by fighter id.
    void resolve_round();

    void resolve_item_action(Fighter& actor, Fighter& target, const ItemDef& item);
    void resolve_action(Fighter& actor, Fighter& target, const SkillDef& requested_skill);
    bool all_ready() const;

    // Target fallback keeps text commands terse: empty target means lowest-HP valid target.
    Fighter* resolve_target(const Fighter& actor, const SkillDef& skill,
                            const std::string& requested_text);

    Fighter* active_player(const std::string& player_name, std::string& error);
    void lock_action(Action action, const std::string& message);
    Fighter* fighter_for_player(const std::string& player_name);
    Fighter* fighter_by_id(int id);
    Fighter* fighter_by_text(const std::string& text);
    std::string side_names(FighterSide side) const;
    std::optional<FighterSide> winner_side() const;
    void finish();
    void add_event(std::string kind, std::string text);

    int id_ = 0;
    BattleMode mode_;
    int round_ = 0;
    std::vector<Fighter> fighters_;
    std::unordered_map<int, Action> pending_actions_;
    std::vector<Event> events_;
    bool closed_ = false;
    const Config& config_;
    std::mt19937& rng_;
    OutputSink& out_;
};

}  // namespace mm
