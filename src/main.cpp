#include "game_core.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

enum class Language
{
    Chinese,
    English,
};

struct MenuItem
{
    std::string zh;
    std::string en;
    std::function<std::string()> build_command;
};

const char* text(Language language, const char* zh, const char* en)
{
    return language == Language::Chinese ? zh : en;
}

void setup_console()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

std::string trim_copy(std::string value)
{
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
        value.pop_back();
    }
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) {
        ++start;
    }
    return value.substr(start);
}

std::string ask(Language language, const char* zh, const char* en)
{
    std::cout << text(language, zh, en);
    std::string value;
    std::getline(std::cin, value);
    return trim_copy(value);
}

std::string append_arg(std::string command, const std::string& arg)
{
    if (!arg.empty()) {
        command += " " + arg;
    }
    return command;
}

Language choose_language()
{
    while (true) {
        std::cout << "Select language / 选择语言:\n"
                  << "  1. 中文\n"
                  << "  2. English\n"
                  << "> ";
        std::string input;
        std::getline(std::cin, input);
        input = trim_copy(input);
        if (input == "1" || input == "zh" || input == "ZH") {
            return Language::Chinese;
        }
        if (input == "2" || input == "en" || input == "EN") {
            return Language::English;
        }
        std::cout << "Invalid choice.\n";
    }
}

std::string with_player(const std::string& command, const std::string& player)
{
    return command + " " + player;
}

std::vector<MenuItem> make_menu(Language language, const std::string& current_player)
{
    const bool logged_in = !current_player.empty();
    std::vector<MenuItem> items;

    if (!logged_in) {
        items.push_back({ "登录玩家", "Login player", [language] {
                             const std::string name = ask(language, "玩家名: ", "Player name: ");
                             return name.empty() ? "" : "login " + name;
                         } });
        items.push_back({ "查看帮助", "Show help", [] { return "help"; } });
        items.push_back({ "查看怪物/遭遇", "List monsters/encounters", [] { return "monsters"; } });
        items.push_back({ "查看道具", "List items", [] { return "items"; } });
        items.push_back({ "查看技能", "List skills", [] { return "skills"; } });
        items.push_back({ "查看任务", "List quests", [] { return "quests"; } });
        items.push_back({ "查看商店", "List shop", [] { return "shop"; } });
        items.push_back({ "读取存档", "Load players", [] { return "load"; } });
        items.push_back({ "手动输入命令", "Manual command", [language] {
                             return ask(language, "命令: ", "Command: ");
                         } });
        items.push_back({ "退出", "Quit", [] { return "quit"; } });
        return items;
    }

    items.push_back({ "开始 PVE", "Start PVE", [language, current_player] {
                         const std::string encounter =
                           ask(language, "遭遇 id，可留空默认 slime: ", "Encounter id, empty for slime: ");
                         return append_arg(with_player("pve", current_player), encounter);
                     } });
    items.push_back({ "加入 PVP 队列", "Queue for PVP", [current_player] {
                         return with_player("queue", current_player);
                     } });
    items.push_back({ "普通攻击", "Attack", [language, current_player] {
                         const std::string target =
                           ask(language, "目标，可留空自动选择: ", "Target, empty for auto: ");
                         return append_arg(with_player("attack", current_player), target);
                     } });
    items.push_back({ "重击", "Heavy strike", [language, current_player] {
                         const std::string target =
                           ask(language, "目标，可留空自动选择: ", "Target, empty for auto: ");
                         return append_arg(with_player("heavy", current_player), target);
                     } });
    items.push_back({ "火符", "Fire charm", [language, current_player] {
                         const std::string target =
                           ask(language, "目标，可留空自动选择: ", "Target, empty for auto: ");
                         return append_arg(with_player("fire", current_player), target);
                     } });
    items.push_back({ "防御", "Defend", [current_player] {
                         return with_player("defend", current_player);
                     } });
    items.push_back({ "治疗", "Heal", [language, current_player] {
                         const std::string target =
                           ask(language, "目标，可留空治疗自己: ", "Target, empty for self: ");
                         return append_arg(with_player("heal", current_player), target);
                     } });
    items.push_back({ "使用道具", "Use item", [language, current_player] {
                         const std::string item = ask(language, "道具 id: ", "Item id: ");
                         const std::string target = ask(language, "目标，可留空自己: ", "Target, empty for self: ");
                         if (item.empty()) {
                             return std::string();
                         }
                         return append_arg(with_player("use", current_player) + " " + item, target);
                     } });
    items.push_back({ "查看背包", "Show inventory", [current_player] {
                         return with_player("inventory", current_player);
                     } });
    items.push_back({ "查看商店", "Show shop", [] { return "shop"; } });
    items.push_back({ "购买道具", "Buy item", [language, current_player] {
                         const std::string item = ask(language, "道具 id: ", "Item id: ");
                         const std::string amount = ask(language, "数量，可留空 1: ", "Amount, empty for 1: ");
                         if (item.empty()) {
                             return std::string();
                         }
                         return append_arg(with_player("buy", current_player) + " " + item, amount);
                     } });
    items.push_back({ "查看可接任务", "List quest templates", [] { return "quests"; } });
    items.push_back({ "接取任务", "Accept quest", [language, current_player] {
                         const std::string quest = ask(language, "任务 id: ", "Quest id: ");
                         return quest.empty() ? "" : "quest accept " + current_player + " " + quest;
                     } });
    items.push_back({ "查看我的任务", "List my quests", [current_player] {
                         return with_player("quest list", current_player);
                     } });
    items.push_back({ "领取任务奖励", "Claim quest", [language, current_player] {
                         const std::string quest = ask(language, "任务 id: ", "Quest id: ");
                         return quest.empty() ? "" : "quest claim " + current_player + " " + quest;
                     } });
    items.push_back({ "认输", "Forfeit", [current_player] {
                         return with_player("forfeit", current_player);
                     } });
    items.push_back({ "查看玩家", "List players", [] { return "players"; } });
    items.push_back({ "查看怪物/遭遇", "List monsters/encounters", [] { return "monsters"; } });
    items.push_back({ "查看道具", "List items", [] { return "items"; } });
    items.push_back({ "查看技能", "List skills", [] { return "skills"; } });
    items.push_back({ "查看服务端状态", "Show server state", [] { return "state"; } });
    items.push_back({ "查看战斗日志", "Show battle log", [language] {
                         const std::string battle =
                           ask(language, "战斗 id，可留空最近一场: ", "Battle id, empty for latest: ");
                         return append_arg("log", battle);
                     } });
    items.push_back({ "保存存档", "Save players", [] { return "save"; } });
    items.push_back({ "读取存档", "Load players", [] { return "load"; } });
    items.push_back({ "手动输入命令", "Manual command", [language] {
                         return ask(language, "命令: ", "Command: ");
                     } });
    items.push_back({ "登出", "Logout", [current_player] {
                         return with_player("logout", current_player);
                     } });
    items.push_back({ "退出", "Quit", [] { return "quit"; } });
    return items;
}

void print_menu(Language language, const std::vector<MenuItem>& items, const std::string& current_player)
{
    std::cout << "\n==== " << text(language, "可用命令", "Available Commands") << " ====";
    if (current_player.empty()) {
        std::cout << "  " << text(language, "未登录", "not logged in") << "\n";
    }
    else {
        std::cout << "  " << text(language, "当前玩家: ", "current player: ") << current_player << "\n";
    }
    for (size_t i = 0; i < items.size(); ++i) {
        std::cout << "  " << (i + 1) << ". " << text(language, items[i].zh.c_str(), items[i].en.c_str()) << "\n";
    }
    std::cout << text(language, "请选择数字: ", "Choose a number: ");
}

int read_choice()
{
    std::string input;
    std::getline(std::cin, input);
    input = trim_copy(input);
    try {
        size_t parsed = 0;
        const int value = std::stoi(input, &parsed);
        return parsed == input.size() ? value : -1;
    }
    catch (const std::exception&) {
        return -1;
    }
}

std::string first_arg_after_command(const std::string& command, const std::string& prefix)
{
    if (command.rfind(prefix, 0) != 0 || command.size() <= prefix.size()) {
        return {};
    }
    const std::string rest = trim_copy(command.substr(prefix.size()));
    const size_t space = rest.find(' ');
    return space == std::string::npos ? rest : rest.substr(0, space);
}

}  // namespace

int main()
{
    setup_console();

    mm::GameServer server;
    const Language language = choose_language();
    std::string current_player;

    std::cout << text(language,
                      "本地文字客户端已启动，菜单使用中文，战斗结果暂由服务端输出原文。\n",
                      "Local text client started. Menu is localized; game results are server text.\n");

    while (true) {
        const std::vector<MenuItem> items = make_menu(language, current_player);
        print_menu(language, items, current_player);
        const int choice = read_choice();
        if (choice < 1 || static_cast<size_t>(choice) > items.size()) {
            std::cout << text(language, "[错误] 无效选项\n", "[error] invalid choice\n");
            continue;
        }

        const std::string command = trim_copy(items[static_cast<size_t>(choice - 1)].build_command());
        if (command.empty()) {
            std::cout << text(language, "[错误] 命令不能为空\n", "[error] command cannot be empty\n");
            continue;
        }

        const std::string output = server.execute(command);
        std::cout << output;

        if (command == "quit") {
            break;
        }

        if (current_player.empty()) {
            const std::string login_name = first_arg_after_command(command, "login");
            if (!login_name.empty() && output.find("[server] " + login_name + " entered the world") != std::string::npos) {
                current_player = login_name;
            }
        }
        else if (command == "logout " + current_player
                 && output.find("[server] " + current_player + " logged out") != std::string::npos) {
            current_player.clear();
        }
    }

    return 0;
}
