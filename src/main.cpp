#include "game_core.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <fstream>
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

std::string lang_config_path()
{
    const char* home = nullptr;
#ifdef _WIN32
    home = std::getenv("USERPROFILE");
    if (!home || home[0] == '\0') {
        home = std::getenv("HOMEDRIVE");
        const char* homepath = std::getenv("HOMEPATH");
        static std::string combined;
        if (home && homepath) {
            combined = std::string(home) + homepath;
            return combined + "/.mmdemo_lang";
        }
    }
#else
    home = std::getenv("HOME");
#endif
    if (home && home[0] != '\0') {
        return std::string(home) + "/.mmdemo_lang";
    }
    return ".mmdemo_lang";
}

void save_language_preference(Language language)
{
    try {
        std::ofstream file(lang_config_path());
        if (file.is_open()) {
            file << (language == Language::Chinese ? "zh" : "en") << "\n";
        }
    }
    catch (...) {
        // 静默忽略保存失败
    }
}

Language load_or_choose_language()
{
    // 尝试从配置文件读取
    try {
        std::ifstream file(lang_config_path());
        if (file.is_open()) {
            std::string line;
            if (std::getline(file, line)) {
                std::string trimmed;
                for (char c : line) {
                    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                        trimmed += c;
                    }
                }
                if (trimmed == "zh" || trimmed == "ZH") {
                    return Language::Chinese;
                }
                if (trimmed == "en" || trimmed == "EN") {
                    return Language::English;
                }
            }
        }
    }
    catch (...) {
        // 读取失败，走选择流程
    }

    // 首次运行，让用户选择
    while (true) {
        std::cout << "Select language / 选择语言:\n"
                  << "  1. 中文\n"
                  << "  2. English\n"
                  << "> ";
        std::string input;
        std::getline(std::cin, input);
        // trim
        while (!input.empty() && (input.back() == ' ' || input.back() == '\t' || input.back() == '\r')) {
            input.pop_back();
        }
        size_t start = 0;
        while (start < input.size() && (input[start] == ' ' || input[start] == '\t')) {
            ++start;
        }
        input = input.substr(start);
        if (input == "1" || input == "zh" || input == "ZH") {
            save_language_preference(Language::Chinese);
            return Language::Chinese;
        }
        if (input == "2" || input == "en" || input == "EN") {
            save_language_preference(Language::English);
            return Language::English;
        }
        std::cout << "Invalid choice.\n";
    }
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

std::string with_player(const std::string& command, const std::string& player)
{
    return command + " " + player;
}

// 分类菜单项：{编号, 中文, 英文, build_command}
struct CatItem
{
    int id;
    std::string zh;
    std::string en;
    std::function<std::string()> build_command;
};

struct CatGroup
{
    const char* zh_title;
    const char* en_title;
    std::vector<CatItem> items;
};

void clear_screen()
{
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

// 格式化一个菜单项为固定宽度字符串，如 " 1.xxx  "
std::string fmt_item(Language lang, int id, const char* zh, const char* en, int width)
{
    std::string s = " " + std::to_string(id) + "." + text(lang, zh, en);
    if (static_cast<int>(s.size()) < width) {
        s.append(width - s.size(), ' ');
    }
    return s;
}

void print_categories(Language lang, const std::vector<CatGroup>& groups,
                      const std::string& current_player)
{
    clear_screen();
    std::cout << "==== " << text(lang, "可用命令", "Available Commands") << " ====";
    if (current_player.empty()) {
        std::cout << "  " << text(lang, "未登录", "not logged in") << "\n\n";
    } else {
        std::cout << "  " << text(lang, "当前玩家: ", "current player: ") << current_player << "\n\n";
    }

    constexpr int kCols = 3;
    constexpr int kColWidth = 26;

    for (const auto& g : groups) {
        std::cout << "-- " << text(lang, g.zh_title, g.en_title) << " --\n";
        const size_t n = g.items.size();
        const size_t rows = (n + kCols - 1) / kCols;
        for (size_t r = 0; r < rows; ++r) {
            for (int c = 0; c < kCols; ++c) {
                const size_t idx = r + c * rows;
                if (idx < n) {
                    const auto& it = g.items[idx];
                    std::cout << fmt_item(lang, it.id, it.zh.c_str(), it.en.c_str(), kColWidth);
                }
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }
    std::cout << text(lang, "请选择数字: ", "Choose a number: ");
}

// 根据编号查找菜单项
const CatItem* find_item(const std::vector<CatGroup>& groups, int id)
{
    for (const auto& g : groups) {
        for (const auto& it : g.items) {
            if (it.id == id) return &it;
        }
    }
    return nullptr;
}

std::vector<CatGroup> make_categories(Language language, const std::string& current_player,
                                      std::function<void()> toggle_lang_callback = {})
{
    const bool logged_in = !current_player.empty();
    int next_id = 0;

    auto next = [&next_id] { return ++next_id; };

    if (!logged_in) {
        std::vector<CatGroup> groups;
        {
            CatGroup g{ "账号", "Account" };
            g.items.push_back({ next(), "登录玩家", "Login player", [language] {
                const std::string name = ask(language, "玩家名: ", "Player name: ");
                return name.empty() ? "" : "login " + name;
            } });
            g.items.push_back({ next(), "查看帮助", "Show help", [] { return "help"; } });
            groups.push_back(std::move(g));
        }
        {
            CatGroup g{ "信息查询", "Info" };
            g.items.push_back({ next(), "查看怪物", "Monsters", [] { return "monsters"; } });
            g.items.push_back({ next(), "查看道具", "Items", [] { return "items"; } });
            g.items.push_back({ next(), "查看技能", "Skills", [] { return "skills"; } });
            g.items.push_back({ next(), "查看任务", "Quests", [] { return "quests"; } });
            g.items.push_back({ next(), "查看商店", "Shop", [] { return "shop"; } });
            groups.push_back(std::move(g));
        }
        {
            CatGroup g{ "系统", "System" };
            g.items.push_back({ next(), "读取存档", "Load players", [] { return "load"; } });
            g.items.push_back({ next(), "手动命令", "Manual cmd", [language] {
                return ask(language, "命令: ", "Command: ");
            } });
            g.items.push_back({ next(), "切换语言", "Switch lang", [toggle_lang_callback] {
                if (toggle_lang_callback) toggle_lang_callback();
                return std::string("__lang_toggle__");
            } });
            g.items.push_back({ next(), "退出", "Quit", [] { return "quit"; } });
            groups.push_back(std::move(g));
        }
        return groups;
    }

    // 已登录
    std::vector<CatGroup> groups;
    {
        CatGroup g{ "战斗", "Battle" };
        g.items.push_back({ next(), "开始PVE", "Start PVE", [language, current_player] {
            const std::string enc = ask(language, "遭遇id(空=slime): ", "Encounter id (empty=slime): ");
            return append_arg(with_player("pve", current_player), enc);
        } });
        g.items.push_back({ next(), "PVP排队", "PVP Queue", [current_player] {
            return with_player("queue", current_player);
        } });
        g.items.push_back({ next(), "攻击", "Attack", [language, current_player] {
            const std::string t = ask(language, "目标(空=auto): ", "Target (empty=auto): ");
            return append_arg(with_player("attack", current_player), t);
        } });
        g.items.push_back({ next(), "重击", "Heavy", [language, current_player] {
            const std::string t = ask(language, "目标(空=auto): ", "Target (empty=auto): ");
            return append_arg(with_player("heavy", current_player), t);
        } });
        g.items.push_back({ next(), "火符", "Fire", [language, current_player] {
            const std::string t = ask(language, "目标(空=auto): ", "Target (empty=auto): ");
            return append_arg(with_player("fire", current_player), t);
        } });
        g.items.push_back({ next(), "防御", "Defend", [current_player] {
            return with_player("defend", current_player);
        } });
        g.items.push_back({ next(), "治疗", "Heal", [language, current_player] {
            const std::string t = ask(language, "目标(空=self): ", "Target (empty=self): ");
            return append_arg(with_player("heal", current_player), t);
        } });
        g.items.push_back({ next(), "使用道具", "Use Item", [language, current_player] {
            const std::string it = ask(language, "道具id: ", "Item id: ");
            if (it.empty()) return std::string();
            const std::string t = ask(language, "目标(空=self): ", "Target (empty=self): ");
            return append_arg(with_player("use", current_player) + " " + it, t);
        } });
        g.items.push_back({ next(), "认输", "Forfeit", [current_player] {
            return with_player("forfeit", current_player);
        } });
        groups.push_back(std::move(g));
    }
    {
        CatGroup g{ "背包/商店", "Bag/Shop" };
        g.items.push_back({ next(), "查看背包", "Inventory", [current_player] {
            return with_player("inventory", current_player);
        } });
        g.items.push_back({ next(), "查看商店", "Shop", [] { return "shop"; } });
        g.items.push_back({ next(), "购买道具", "Buy item", [language, current_player] {
            const std::string it = ask(language, "道具id: ", "Item id: ");
            if (it.empty()) return std::string();
            const std::string n = ask(language, "数量(空=1): ", "Amount (empty=1): ");
            return append_arg(with_player("buy", current_player) + " " + it, n);
        } });
        groups.push_back(std::move(g));
    }
    {
        CatGroup g{ "任务", "Quest" };
        g.items.push_back({ next(), "可接任务", "Templates", [] { return "quests"; } });
        g.items.push_back({ next(), "接取任务", "Accept", [language, current_player] {
            const std::string q = ask(language, "任务id: ", "Quest id: ");
            return q.empty() ? "" : "quest accept " + current_player + " " + q;
        } });
        g.items.push_back({ next(), "我的任务", "My quests", [current_player] {
            return with_player("quest list", current_player);
        } });
        g.items.push_back({ next(), "领取奖励", "Claim", [language, current_player] {
            const std::string q = ask(language, "任务id: ", "Quest id: ");
            return q.empty() ? "" : "quest claim " + current_player + " " + q;
        } });
        groups.push_back(std::move(g));
    }
    {
        CatGroup g{ "信息", "Info" };
        g.items.push_back({ next(), "玩家列表", "Players", [] { return "players"; } });
        g.items.push_back({ next(), "怪物列表", "Monsters", [] { return "monsters"; } });
        g.items.push_back({ next(), "道具列表", "Items", [] { return "items"; } });
        g.items.push_back({ next(), "技能列表", "Skills", [] { return "skills"; } });
        g.items.push_back({ next(), "服务端状态", "ServerState", [] { return "state"; } });
        g.items.push_back({ next(), "战斗日志", "BattleLog", [language] {
            const std::string b = ask(language, "战斗id(空=最近): ", "Battle id (empty=latest): ");
            return append_arg("log", b);
        } });
        groups.push_back(std::move(g));
    }
    {
        CatGroup g{ "系统", "System" };
        g.items.push_back({ next(), "保存存档", "Save", [] { return "save"; } });
        g.items.push_back({ next(), "读取存档", "Load", [] { return "load"; } });
        g.items.push_back({ next(), "手动命令", "Manual", [language] {
            return ask(language, "命令: ", "Command: ");
        } });
        g.items.push_back({ next(), "登出", "Logout", [current_player] {
            return with_player("logout", current_player);
        } });
        g.items.push_back({ next(), "退出", "Quit", [] { return "quit"; } });
        groups.push_back(std::move(g));
    }
    return groups;
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
    Language language = load_or_choose_language();
    std::string current_player;

    while (true) {
        auto toggle_lang = [&language] {
            language = (language == Language::Chinese) ? Language::English : Language::Chinese;
            save_language_preference(language);
        };
        const auto groups = make_categories(language, current_player, toggle_lang);
        print_categories(language, groups, current_player);
        const int choice = read_choice();
        const auto* item = find_item(groups, choice);
        if (!item) {
            std::cout << text(language, "[错误] 无效选项\n", "[error] invalid choice\n");
            continue;
        }

        const std::string command = trim_copy(item->build_command());
        if (command == "__lang_toggle__") {
            std::cout << text(language, "语言已切换。\n", "Language switched.\n");
            continue;
        }
        if (command.empty()) {
            std::cout << text(language, "[错误] 命令不能为空\n", "[error] command cannot be empty\n");
            continue;
        }

        const std::string output = server.execute(command);
        std::cout << output;

        // 执行完毕后暂停，让用户看到结果
        std::cout << text(language, "\n按回车继续...", "\nPress Enter to continue...");
        std::string dummy;
        std::getline(std::cin, dummy);

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
