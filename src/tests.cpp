#define MMDEMO_NO_MAIN
#include "game_core.h"
#include "net/SessionCommand.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

// Tiny no-framework test harness. Each test drives GameServer through the same text commands
// used by the CLI/TCP layers, so these are protocol-level regression tests.
struct TestContext
{
    int failed = 0;
};

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

void expect_contains(TestContext& ctx, const std::string& label, const std::string& text,
                     const std::string& needle)
{
    if (contains(text, needle)) {
        std::cout << "[PASS] " << label << "\n";
        return;
    }

    ++ctx.failed;
    std::cout << "[FAIL] " << label << "\n"
              << "  expected: " << needle << "\n"
              << "  actual:\n"
              << text << "\n";
}

void expect_not_contains(TestContext& ctx, const std::string& label, const std::string& text,
                         const std::string& needle)
{
    if (!contains(text, needle)) {
        std::cout << "[PASS] " << label << "\n";
        return;
    }

    ++ctx.failed;
    std::cout << "[FAIL] " << label << "\n"
              << "  unexpected: " << needle << "\n"
              << "  actual:\n"
              << text << "\n";
}

std::string run(mm::GameServer& server, const std::vector<std::string>& commands)
{
    std::string output;
    for (const auto& command : commands) {
        output += server.execute(command);
    }
    return output;
}

void test_command_catalog_and_introspection(TestContext& ctx)
{
    // Read-only commands are part of the player-facing protocol and catch missing registrations.
    mm::GameServer server("test_players_catalog.json", 7);
    const std::string output = run(server, {
                                             "help",
                                             "monsters",
                                             "items",
                                             "skills",
                                             "quests",
                                             "shop",
                                             "quit",
                                             "nonsense",
                                           });

    expect_contains(ctx, "help lists pve", output, "pve <name> [encounter]");
    expect_contains(ctx, "monster catalog", output, "[monster] slime");
    expect_contains(ctx, "encounter catalog", output, "[encounter] forest");
    expect_contains(ctx, "item catalog", output, "[item] potion");
    expect_contains(ctx, "skill catalog", output, "[skill] fire");
    expect_contains(ctx, "quest catalog", output, "[quest] slime_hunter");
    expect_contains(ctx, "shop catalog", output, "[shop] hi_potion");
    expect_contains(ctx, "quit command", output, "[server] goodbye");
    expect_contains(ctx, "unknown command", output, "[error] unknown command");
}

void expect_equal(TestContext& ctx, const std::string& label, const std::string& actual,
                  const std::string& expected)
{
    if (actual == expected) {
        std::cout << "[PASS] " << label << "\n";
        return;
    }

    ++ctx.failed;
    std::cout << "[FAIL] " << label << "\n"
              << "  expected: " << expected << "\n"
              << "  actual: " << actual << "\n";
}

void test_tcp_session_command_translation(TestContext& ctx)
{
    // TCP shorthand must be deterministic and must reject attempts to act as another player.
    std::string error;

    expect_equal(ctx, "tcp pve shorthand",
                 mm::translate_session_command("pve forest", "alice", error), "pve alice forest");
    expect_equal(ctx, "tcp queue shorthand", mm::translate_session_command("queue", "alice", error),
                 "queue alice");
    expect_equal(ctx, "tcp attack target shorthand",
                 mm::translate_session_command("heavy bob", "alice", error), "heavy alice bob");
    expect_equal(ctx, "tcp use item shorthand",
                 mm::translate_session_command("use potion", "alice", error), "use alice potion");
    expect_equal(ctx, "tcp buy shorthand",
                 mm::translate_session_command("buy potion 2", "alice", error),
                 "buy alice potion 2");
    expect_equal(ctx, "tcp quest accept shorthand",
                 mm::translate_session_command("quest accept slime_hunter", "alice", error),
                 "quest accept alice slime_hunter");
    expect_equal(ctx, "tcp quest list shorthand",
                 mm::translate_session_command("quest list", "alice", error), "quest list alice");
    expect_equal(ctx, "tcp explicit own command",
                 mm::translate_session_command("pve alice slime", "alice", error),
                 "pve alice slime");

    error.clear();
    expect_equal(ctx, "tcp reject other player",
                 mm::translate_session_command("forfeit bob", "alice", error), "");
    expect_contains(ctx, "tcp reject error text", error, "bound to alice");

    expect_equal(ctx, "tcp prelogin help allowed",
                 mm::is_valid_tcp_command_without_login("help") ? "yes" : "no", "yes");
    expect_equal(ctx, "tcp prelogin pve rejected",
                 mm::is_valid_tcp_command_without_login("pve") ? "yes" : "no", "no");
}

void test_pve_drop(TestContext& ctx)
{
    // Basic PVE loop: login, battle, reward, drop, and profile summary.
    mm::GameServer server;
    const std::string output = run(server, {
                                             "login alice",
                                             "pve alice slime",
                                             "heavy alice",
                                             "heavy alice",
                                             "heavy alice",
                                             "heavy alice",
                                             "players",
                                           });

    expect_contains(ctx, "pve battle starts", output, "Battle B1 starts: alice vs Green Slime#1");
    expect_contains(ctx, "pve win reward", output, "[reward] alice receives 12 exp and 8 gold");
    expect_contains(ctx, "pve item drop", output, "[reward] alice receives item potion x1");
    expect_contains(ctx, "pve player inventory", output, "inventory=potionx1");
}

void test_inventory_and_item_use(TestContext& ctx)
{
    // Inventory commands and battle item consumption share the same durable player inventory.
    mm::GameServer server("test_players_inventory.json", 11);
    const std::string output = run(server, {
                                             "login alice",
                                             "give alice potion 2",
                                             "inventory alice",
                                             "bag alice",
                                             "pve alice slime",
                                             "use alice potion",
                                             "forfeit alice",
                                             "players",
                                           });

    expect_contains(ctx, "give item", output, "alice receives potion x2");
    expect_contains(ctx, "inventory command", output, "[inventory] alice: potionx2");
    expect_contains(ctx, "bag alias", output, "[inventory] alice: potionx2");
    expect_contains(ctx, "battle item action", output, "uses Small Potion on alice");
    expect_contains(ctx, "item consumed", output, "inventory=potionx1");
}

void test_active_battle_skills_state_and_log(TestContext& ctx)
{
    // Active battle inspection covers state/log commands plus the defend skill branch.
    mm::GameServer server("test_players_battle.json", 17);
    const std::string output = run(server, {
                                             "login alice",
                                             "pve alice slime",
                                             "state",
                                             "defend alice",
                                             "log",
                                             "pve alice slime",
                                           });

    expect_contains(ctx, "state active battle", output, "[server] active battles: B1");
    expect_contains(ctx, "defend skill event", output, "alice braces for impact.");
    expect_contains(ctx, "log command reads active battle", output,
                    "[log][0][start] Battle B1 starts");
    expect_contains(ctx, "cannot start pve while battling", output, "alice is already in battle");
}

void test_pvp_matchmaking_and_actions(TestContext& ctx)
{
    // PVP uses the same Battle engine as PVE, but battle creation comes from matchmaking.
    mm::GameServer server("test_players_pvp.json", 23);
    const std::string output = run(server, {
                                             "login alice",
                                             "login bob",
                                             "queue alice",
                                             "queue bob",
                                             "fire alice bob",
                                             "heal bob bob",
                                             "state",
                                           });

    expect_contains(ctx, "first pvp queue", output, "alice queued");
    expect_contains(ctx, "pvp battle starts", output, "Battle B1 starts: alice vs bob");
    expect_contains(ctx, "fire action locked", output, "alice -> Fire Charm / bob");
    expect_contains(ctx, "heal action locked", output, "bob -> Heal / bob");
    expect_contains(ctx, "pvp active battle", output, "[server] active battles: B1");
}

void test_multi_monster_encounter(TestContext& ctx)
{
    // Multi-monster encounters verify aggregate rewards and kill-map quest updates.
    mm::GameServer server;
    const std::string output = run(server, {
                                             "login alice",
                                             "give alice hi_potion 1",
                                             "quest accept alice fox_trouble",
                                             "pve alice forest",
                                             "heavy alice fox2",
                                             "heavy alice fox2",
                                             "heavy alice fox2",
                                             "use alice hi_potion",
                                             "attack alice",
                                             "attack alice",
                                             "attack alice",
                                             "attack alice",
                                             "attack alice",
                                             "quest claim alice fox_trouble",
                                             "players",
                                           });

    expect_contains(ctx, "encounter enter", output, "alice enters Forest Patrol");
    expect_contains(ctx, "encounter battle starts", output,
                    "Battle B1 starts: alice vs Green Slime#1, Mountain Fox#2");
    expect_contains(ctx, "encounter reward sum", output,
                    "[reward] alice receives 30 exp and 20 gold");
    expect_contains(ctx, "encounter quest progress", output, "Fox Trouble progress 1/1");
    expect_contains(ctx, "encounter quest claimed", output, "claimed Fox Trouble reward");
}

void test_quest_and_shop(TestContext& ctx)
{
    // Long-form progression loop: quests unlock rewards, rewards fund shop purchases.
    mm::GameServer server;
    const std::string output = run(server, {
                                             "login alice",
                                             "quest accept alice slime_hunter",
                                             "pve alice slime",
                                             "heavy alice",
                                             "heavy alice",
                                             "heavy alice",
                                             "heavy alice",
                                             "pve alice slime",
                                             "heavy alice",
                                             "heavy alice",
                                             "heavy alice",
                                             "heavy alice",
                                             "quest claim alice slime_hunter",
                                             "buy alice potion 1",
                                             "players",
                                           });

    expect_contains(ctx, "quest accepted", output, "alice accepted Slime Hunter");
    expect_contains(ctx, "quest completed", output, "Slime Hunter progress 2/2");
    expect_contains(ctx, "quest claimed", output, "claimed Slime Hunter reward");
    expect_contains(ctx, "shop purchase", output, "bought potion x1");
    expect_contains(ctx, "quest player summary", output, "quests=slime_hunter:2/2:claimed");
}

void test_save_and_load(TestContext& ctx)
{
    // Save/load must restore durable profile data without restoring volatile online/battle state.
    const char* file_name = "test_players_save_load.json";
    std::remove(file_name);

    {
        mm::GameServer server(file_name, 31);
        const std::string output = run(server, {
                                                 "login alice",
                                                 "give alice potion 3",
                                                 "quest accept alice slime_hunter",
                                                 "pve alice slime",
                                                 "heavy alice",
                                                 "heavy alice",
                                                 "heavy alice",
                                                 "heavy alice",
                                                 "save",
                                               });

        expect_contains(ctx, "save writes custom file", output,
                        "saved 1 players to test_players_save_load.json");
    }

    {
        mm::GameServer server(file_name, 31);
        const std::string output = run(server, {
                                                 "load",
                                                 "players",
                                               });

        expect_contains(ctx, "load reads custom file", output,
                        "loaded 1 players from test_players_save_load.json");
        expect_contains(ctx, "load restores player", output, "alice level=1");
        expect_contains(ctx, "load restores inventory", output, "inventory=potionx4");
        expect_contains(ctx, "load restores quest", output, "quests=slime_hunter:1/2");
        expect_contains(ctx, "load keeps player offline", output,
                        "alice level=1 exp=12/40 gold=8 offline");
    }

    std::remove(file_name);
}

void test_forfeit_logout_reconnect(TestContext& ctx)
{
    // Session lifecycle commands should cleanly detach players from battle and online state.
    mm::GameServer server;
    const std::string output = run(server, {
                                             "login alice",
                                             "pve alice slime",
                                             "forfeit alice",
                                             "logout alice",
                                             "login alice",
                                             "players",
                                           });

    expect_contains(ctx, "forfeit event", output, "alice forfeits the battle.");
    expect_contains(ctx, "forfeit winner", output, "Green Slime#1 wins.");
    expect_contains(ctx, "logout after forfeit", output, "alice logged out");
    expect_contains(ctx, "reconnect after logout", output, "alice reconnected");
    expect_contains(ctx, "player online after reconnect", output, "alice level=1");
}

void test_json_config_loading(TestContext& ctx)
{
    // Config data is loaded through nlohmann/json from an external JSON file.
    const char* config_file = "test_config.json";
    const char* player_file = "test_players_json.json";
    std::remove(config_file);
    std::remove(player_file);

    {
        std::ofstream file(config_file, std::ios::trunc);
        file << R"json({
  "items": [
    {"id": "berry", "name": "Battle Berry", "heal": 20, "price": 3, "description": "Test item."}
  ],
  "monsters": [
    {"id": "dummy", "name": "Training Dummy", "level": 1, "max_hp": 20, "max_mp": 0, "attack": 1, "defense": 0, "speed": 1, "exp": 5, "gold": 2, "drops": {"berry": 1}, "skills": ["attack"]}
  ],
  "quests": [
    {"id": "dummy_trial", "name": "Dummy Trial", "target_monster": "dummy", "required_kills": 1, "reward_exp": 7, "reward_gold": 4, "reward_items": {"berry": 1}}
  ],
  "encounters": [
    {"id": "trial", "name": "JSON Trial", "monsters": ["dummy"]}
  ]
})json";
    }

    mm::GameServer server(player_file, 41, config_file);
    const std::string output = run(server, {
                                             "login alice",
                                             "items",
                                             "monsters",
                                             "quests",
                                             "quest accept alice dummy_trial",
                                             "pve alice trial",
                                             "heavy alice",
                                             "quest claim alice dummy_trial",
                                             "players",
                                           });

    expect_contains(ctx, "json item catalog", output, "[item] berry name=Battle Berry");
    expect_contains(ctx, "json encounter enters", output, "alice enters JSON Trial");
    expect_contains(ctx, "json monster battle", output,
                    "Battle B1 starts: alice vs Training Dummy#1");
    expect_contains(ctx, "json reward", output, "alice receives 5 exp and 2 gold");
    expect_contains(ctx, "json drop", output, "alice receives item berry x1");
    expect_contains(ctx, "json quest progress", output, "Dummy Trial progress 1/1");
    expect_contains(ctx, "json quest claimed", output, "claimed Dummy Trial reward");
    expect_contains(ctx, "json final inventory", output, "inventory=berryx2");

    std::remove(config_file);
    std::remove(player_file);
}

void test_validation_errors(TestContext& ctx)
{
    // Negative paths protect command validation and prevent accidental silent state mutation.
    mm::GameServer server;
    const std::string output = run(server, {
                                             "login alice",
                                             "login alice",
                                             "logout bob",
                                             "pve alice dragon",
                                             "quest accept alice missing",
                                             "quest accept alice slime_hunter",
                                             "quest accept alice slime_hunter",
                                             "quest claim alice slime_hunter",
                                             "log B99",
                                             "login alice",
                                             "use alice potion",
                                             "buy alice hi_potion 1",
                                             "quest claim alice slime_hunter",
                                           });

    expect_contains(ctx, "duplicate login", output, "alice is already online");
    expect_contains(ctx, "unknown player", output, "unknown player: bob");
    expect_contains(ctx, "unknown encounter", output, "unknown encounter: dragon");
    expect_contains(ctx, "unknown quest", output, "unknown quest: missing");
    expect_contains(ctx, "duplicate quest accept", output, "already has slime_hunter");
    expect_contains(ctx, "incomplete quest claim", output, "alice progress 0/2");
    expect_contains(ctx, "unknown battle log", output, "unknown battle: B99");
    expect_contains(ctx, "use item outside battle", output, "alice is not in battle");
    expect_contains(ctx, "buy without gold", output, "alice lacks gold");
    expect_not_contains(ctx, "no save/load test leakage", output, "test_players_save_load");
}

}  // namespace

int main()
{
    TestContext ctx;
    test_command_catalog_and_introspection(ctx);
    test_tcp_session_command_translation(ctx);
    test_pve_drop(ctx);
    test_inventory_and_item_use(ctx);
    test_active_battle_skills_state_and_log(ctx);
    test_pvp_matchmaking_and_actions(ctx);
    test_multi_monster_encounter(ctx);
    test_quest_and_shop(ctx);
    test_save_and_load(ctx);
    test_json_config_loading(ctx);
    test_forfeit_logout_reconnect(ctx);
    test_validation_errors(ctx);

    if (ctx.failed != 0) {
        std::cout << "[RESULT] failed=" << ctx.failed << "\n";
        return 1;
    }

    std::cout << "[RESULT] all tests passed\n";
    return 0;
}
