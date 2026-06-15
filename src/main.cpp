#include "game_core.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

enum class Language {
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

// 跨平台安全的环境变量获取
std::string safe_getenv(const char* name)
{
#ifdef _WIN32
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name) == 0 && buf != nullptr) {
        std::string result(buf);
        free(buf);
        return result;
    }
    return {};
#else
    const char* val = std::getenv(name);
    return val ? std::string(val) : std::string();
#endif
}

std::string lang_config_path()
{
    std::string home;
#ifdef _WIN32
    home = safe_getenv("USERPROFILE");
    if (home.empty()) {
        const std::string drive = safe_getenv("HOMEDRIVE");
        const std::string path = safe_getenv("HOMEPATH");
        if (!drive.empty() && !path.empty()) {
            return drive + path + "/.mmdemo_lang";
        }
    }
#else
    home = safe_getenv("HOME");
#endif
    if (!home.empty()) {
        return home + "/.mmdemo_lang";
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
        while (!input.empty()
               && (input.back() == ' ' || input.back() == '\t' || input.back() == '\r')) {
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
    while (!value.empty()
           && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
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

void print_top_bar(Language lang, const std::string& current_player, bool in_battle)
{
    std::cout << "==== " << text(lang, "MMDemo", "MMDemo") << " ====";
    if (current_player.empty()) {
        std::cout << "  " << text(lang, "未登录", "not logged in") << "\n\n";
    }
    else {
        std::cout << "  " << text(lang, "玩家: ", "Player: ") << current_player;
        if (in_battle) {
            std::cout << "  [" << text(lang, "战斗中", "In Battle") << "]";
        }
        std::cout << "\n\n";
    }
}

// 将服务端标签 [server][battle][state][error] 等翻译为当前语言
std::string translate_tags(Language lang, const std::string& text)
{
    if (text.empty()) return text;

    struct TagPair
    {
        const char* en_tag;
        const char* zh_tag;
    };
    static const TagPair tags[] = {
        { "[server]", "\u670d\u52a1\u5668" },  // 服务器
        { "[battle]", "\u6218\u6597" },        // 战斗
        { "[state]", "\u72b6\u6001" },         // 状态
        { "[error]", "\u9519\u8bef" },         // 错误
        { "[player]", "\u73a9\u5bb6" },        // 玩家
        { "[monster]", "\u602a\u7269" },       // 怪物
        { "[item]", "\u9053\u5177" },          // 道具
        { "[shop]", "\u5546\u5e97" },          // 商店
        { "[quest]", "\u4efb\u52a1" },         // 任务
        { "[reward]", "\u5956\u52b1" },        // 奖励
        { "[skill]", "\u6280\u80fd" },         // 技能
        { "[inventory]", "\u80cc\u5305" },     // 背包
        { "[encounter]", "\u906d\u9047" },     // 遭遇
        { "[log]", "\u65e5\u5fd7" },           // 日志
    };

    if (lang == Language::Chinese) {
        std::string result = text;
        for (const auto& tp : tags) {
            size_t pos = 0;
            while ((pos = result.find(tp.en_tag, pos)) != std::string::npos) {
                result.replace(pos, std::strlen(tp.en_tag), std::string("[") + tp.zh_tag + "]");
                pos += 2 + std::strlen(tp.zh_tag);
            }
        }
        return result;
    }
    return text;
}

// 完整翻译服务端消息（替换英文短语为中文）。HP/MP 等数值标识保留原样。
std::string translate_message(Language lang, const std::string& text)
{
    if (text.empty() || lang != Language::Chinese) return text;

    std::string result = translate_tags(lang, text);

    struct MsgPair
    {
        const char* en;
        const char* zh;
    };
    static const MsgPair msgs[] = {
        // -- Battle.cpp 输出 --
        { "Battle ", "\u6218\u6597 " },
        { " starts: ", " \u5f00\u59cb: " },
        { " vs ", " \u5bf9\u6218 " },
        { "Round ", "\u7b2c " },
        { " begins.", " \u56de\u5408\u5f00\u59cb\u3002" },
        { " uses ", " \u5bf9 " },
        { " on ", " \u4f7f\u7528 " },
        { " damage.", " \u70b9\u4f24\u5bb3\u3002" },
        { " falls.", " \u5012\u4e0b\u4e86\u3002" },
        { " wins.", " \u83b7\u80dc\u3002" },
        { " forfeits the battle.", " \u653e\u5f03\u4e86\u6218\u6597\u3002" },
        { " braces for impact.", " \u505a\u597d\u9632\u5fa1\u51c6\u5907\u3002" },
        { " tried ", " \u8bd5\u56fe\u4f7f\u7528 " },
        { " but lacked MP, using Attack.",
          " \u4f46 MP \u4e0d\u8db3\uff0c\u6539\u7528 \u653b\u51fb\u3002" },
        { " +", " +" },
        { " HP.", " HP\u3002" },
        { "action locked: ", "\u884c\u52a8\u9501\u5b9a: " },
        { " -> ", " -> " },
        { " / ", " / " },

        // -- GameServer.cpp 输出 --
        { "goodbye", "\u518d\u89c1" },
        { "unknown command, type help",
          "\u672a\u77e5\u547d\u4ee4\uff0c\u8f93\u5165 help \u67e5\u770b\u5e2e\u52a9" },
        { "name is required", "\u9700\u8981\u63d0\u4f9b\u540d\u79f0" },
        { " is already online", " \u5df2\u7ecf\u5728\u7ebf" },
        { " reconnected", " \u91cd\u65b0\u8fde\u63a5" },
        { " entered the world", " \u8fdb\u5165\u4e86\u4e16\u754c" },
        { " is in battle, cannot logout",
          " \u5728\u6218\u6597\u4e2d\uff0c\u65e0\u6cd5\u767b\u51fa" },
        { " logged out", " \u5df2\u767b\u51fa" },
        { " is not in battle", " \u4e0d\u5728\u6218\u6597\u4e2d" },
        { "battle disappeared", "\u6218\u6597\u5df2\u6d88\u5931" },
        { " is already in battle", " \u5df2\u7ecf\u5728\u6218\u6597\u4e2d" },
        { "unknown encounter: ", "\u672a\u77e5\u906d\u9047: " },
        { " references unknown monster: ", " \u5f15\u7528\u4e86\u672a\u77e5\u602a\u7269: " },
        { "encounter ", "\u906d\u9047 " },
        { " enters ", " \u8fdb\u5165\u4e86 " },
        { " is already queued", " \u5df2\u7ecf\u5728\u6392\u961f" },
        { " queued", " \u5df2\u6392\u961f" },
        { "item id is required", "\u9700\u8981\u63d0\u4f9b\u9053\u5177ID" },
        { "unknown item: ", "\u672a\u77e5\u9053\u5177: " },
        { " does not have ", " \u6ca1\u6709 " },
        { "player name is required", "\u9700\u8981\u63d0\u4f9b\u73a9\u5bb6\u540d" },
        { "unknown player: ", "\u672a\u77e5\u73a9\u5bb6: " },
        { "no players", "\u65e0\u73a9\u5bb6" },
        { " receives ", " \u83b7\u5f97\u4e86 " },
        { " exp and ", " \u7ecf\u9a8c\u548c " },
        { " gold", " \u91d1\u5e01" },
        { ". Level up to ", "\u3002\u5347\u7ea7\u5230 " },
        { "!", "\u7ea7\uff01" },
        { " receives item ", " \u83b7\u5f97\u4e86\u9053\u5177 " },
        { " progress ", " \u8fdb\u5ea6 " },
        { " bought ", " \u8d2d\u4e70\u4e86 " },
        { " for ", " \uff0c\u82b1\u8d39 " },
        { " lacks gold. need=", " \u91d1\u5e01\u4e0d\u8db3\u3002\u9700\u8981=" },
        { " have=", " \u6301\u6709=" },
        { " accepted ", " \u63a5\u53d7\u4e86 " },
        { " already has ", " \u5df2\u7ecf\u62e5\u6709 " },
        { " has no quests", " \u6ca1\u6709\u4efb\u52a1" },
        { " claimed ", " \u9886\u53d6\u4e86 " },
        { " reward ", " \u5956\u52b1 " },
        { " has not accepted ", " \u5c1a\u672a\u63a5\u53d7 " },
        { " already claimed ", " \u5df2\u7ecf\u9886\u53d6\u4e86 " },
        { "saved ", "\u5df2\u4fdd\u5b58 " },
        { " players to ", " \u4e2a\u73a9\u5bb6\u5230 " },
        { "loaded ", "\u4ece " },
        { " players from ", " \u52a0\u8f7d\u4e86 " },
        { "cannot open ", "\u65e0\u6cd5\u6253\u5f00 " },
        { "invalid player json: ", "\u65e0\u6548\u7684\u73a9\u5bb6\u6570\u636e: " },
        { "unknown battle: ", "\u672a\u77e5\u6218\u6597: " },
        { "unknown quest: ", "\u672a\u77e5\u4efb\u52a1: " },
        { "quest command: accept/list/claim", "\u4efb\u52a1\u547d\u4ee4: accept/list/claim" },
        { " online", " \u5728\u7ebf" },
        { " offline", " \u79bb\u7ebf" },
        { " battle=", " \u6218\u6597=" },
        { " inventory=", " \u80cc\u5305=" },
        { " quests=", " \u4efb\u52a1=" },
        { "queue:", "\u6392\u961f:" },
        { "active battles:", "\u6d3b\u8dc3\u6218\u6597:" },
        { "commands:", "\u547d\u4ee4:" },
        { "create a player session", "\u521b\u5efa\u73a9\u5bb6\u4f1a\u8bdd" },
        { "mark player offline when not in battle",
          "\u5c06\u73a9\u5bb6\u6807\u8bb0\u4e3a\u79bb\u7ebf\uff08\u975e\u6218\u6597\u4e2d\uff09" },
        { "concede current battle", "\u653e\u5f03\u5f53\u524d\u6218\u6597" },
        { "start PVE: slime / fox / bandit / forest / camp",
          "\u5f00\u59cb PVE: slime / fox / bandit / forest / camp" },
        { "enter 1v1 matchmaking", "\u8fdb\u5165 1v1 \u5339\u914d" },
        { "normal attack", "\u666e\u901a\u653b\u51fb" },
        { "heavy strike, costs 8 MP", "\u91cd\u51fb\uff0c\u6d88\u80178 MP" },
        { "alias of heavy", "\u91cd\u51fb\u7684\u522b\u540d" },
        { "fire charm, costs 10 MP", "\u706b\u7130\u672f\uff0c\u6d88\u801710 MP" },
        { "reduce incoming damage this round",
          "\u672c\u56de\u5408\u51cf\u5c11\u53d7\u5230\u7684\u4f24\u5bb3" },
        { "heal self or ally, costs 6 MP",
          "\u6cbb\u7597\u81ea\u5df1\u6216\u961f\u53cb\uff0c\u6d88\u80176 MP" },
        { "use a battle item, e.g. use alice potion",
          "\u4f7f\u7528\u6218\u6597\u9053\u5177\uff0c\u4f8b\u5982 use alice potion" },
        { "list player inventory", "\u67e5\u770b\u73a9\u5bb6\u80cc\u5305" },
        { "debug grant item", "\u8c03\u8bd5\u7528\uff1a\u8d60\u4e0e\u9053\u5177" },
        { "list shop items", "\u67e5\u770b\u5546\u5e97\u9053\u5177" },
        { "buy items with gold", "\u7528\u91d1\u5e01\u8d2d\u4e70\u9053\u5177" },
        { "list quest templates", "\u67e5\u770b\u4efb\u52a1\u6a21\u677f" },
        { "accept quest", "\u63a5\u53d7\u4efb\u52a1" },
        { "list player quests", "\u67e5\u770b\u73a9\u5bb6\u4efb\u52a1" },
        { "claim completed quest", "\u9886\u53d6\u5df2\u5b8c\u6210\u4efb\u52a1\u5956\u52b1" },
        { "list player profiles", "\u67e5\u770b\u73a9\u5bb6\u4fe1\u606f" },
        { "list monster templates", "\u67e5\u770b\u602a\u7269\u6a21\u677f" },
        { "list item templates", "\u67e5\u770b\u9053\u5177\u6a21\u677f" },
        { "list skill templates", "\u67e5\u770b\u6280\u80fd\u6a21\u677f" },
        { "print server state", "\u6253\u5370\u670d\u52a1\u5668\u72b6\u6001" },
        { "print latest or selected battle log",
          "\u6253\u5370\u6700\u8fd1\u6216\u6307\u5b9a\u6218\u6597\u65e5\u5fd7" },
        { "save players to data/players.json", "\u4fdd\u5b58\u73a9\u5bb6\u5230 data/players.json" },
        { "load players from data/players.json",
          "\u4ece data/players.json \u52a0\u8f7d\u73a9\u5bb6" },
        { "show commands", "\u663e\u793a\u547d\u4ee4" },
        { "exit", "\u9000\u51fa" },
    };

    for (const auto& mp : msgs) {
        size_t pos = 0;
        while ((pos = result.find(mp.en, pos)) != std::string::npos) {
            result.replace(pos, std::strlen(mp.en), mp.zh);
            pos += std::strlen(mp.zh);
        }
    }

    return result;
}

// 从 last_output 中分离 [state] 行和其他行，[state] 优先显示
void print_message_area(Language lang, const std::string& last_output)
{
    if (last_output.empty()) return;

    std::string translated = translate_message(lang, last_output);

    std::vector<std::string> state_lines;
    std::vector<std::string> other_lines;
    {
        std::istringstream iss(translated);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            if (line.find("[state]") != std::string::npos
                || line.find("\u72b6\u6001") != std::string::npos) {
                state_lines.push_back(line);
            }
            else {
                other_lines.push_back(line);
            }
        }
    }

    std::cout << "----------------------------------------\n";
    std::cout << text(lang, "[消息]", "[Messages]") << "\n";

    for (const auto& s : state_lines) {
        std::cout << s << "\n";
    }
    for (const auto& o : other_lines) {
        std::cout << o << "\n";
    }

    std::cout << "----------------------------------------\n";
}

// 一级分类菜单（顶层）
struct TopCatItem
{
    int id;
    std::string zh;
    std::string en;
};

void print_top_categories(Language lang, const std::vector<TopCatItem>& cats,
                          const std::string& current_player, bool in_battle,
                          const std::string& last_output)
{
    clear_screen();

    // 战斗时最上方显示HP/MP等信息（从last_output中提取）
    if (in_battle) {
        // 从上次输出中提取状态行
        std::istringstream iss(last_output);
        std::string line;
        bool has_state = false;
        while (std::getline(iss, line)) {
            // HP/MP 状态行格式: "[id] name side HP=xxx/yyy MP=xxx/yyy"
            if (line.find("HP=") != std::string::npos || line.find("hp=") != std::string::npos) {
                if (!has_state) {
                    std::cout << text(lang, "==== 战斗状态 ====", "==== Battle Status ====")
                              << "\n";
                    has_state = true;
                }
                std::cout << line << "\n";
            }
        }
        if (has_state) {
            std::cout << "\n";
        }
    }

    print_top_bar(lang, current_player, in_battle);

    std::cout << text(lang, "-- 请选择分类 --", "-- Select Category --") << "\n\n";

    constexpr int kCols = 3;
    constexpr int kColWidth = 24;

    const size_t n = cats.size();
    const size_t rows = (n + kCols - 1) / kCols;
    for (size_t r = 0; r < rows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            const size_t idx = r + c * rows;
            if (idx < n) {
                const auto& it = cats[idx];
                std::cout << fmt_item(lang, it.id, it.zh.c_str(), it.en.c_str(), kColWidth);
            }
        }
        std::cout << "\n";
    }
    std::cout << "\n";

    // 特殊选项：返回/退出
    std::cout << " 0." << text(lang, "退出", "Quit");
    if (in_battle) {
        std::cout << "  99." << text(lang, "刷新状态", "Refresh State");
    }
    std::cout << "\n\n";

    // 消息区域在下方
    print_message_area(lang, last_output);

    std::cout << text(lang, "请选择: ", "Choose: ");
}

// 二级子菜单（具体命令）
void print_sub_menu(Language lang, const CatGroup& group, const std::string& current_player,
                    bool in_battle, const std::string& last_output)
{
    clear_screen();

    if (in_battle) {
        std::istringstream iss(last_output);
        std::string line;
        bool has_state = false;
        while (std::getline(iss, line)) {
            if (line.find("HP=") != std::string::npos || line.find("hp=") != std::string::npos) {
                if (!has_state) {
                    std::cout << text(lang, "==== 战斗状态 ====", "==== Battle Status ====")
                              << "\n";
                    has_state = true;
                }
                std::cout << line << "\n";
            }
        }
        if (has_state) {
            std::cout << "\n";
        }
    }

    print_top_bar(lang, current_player, in_battle);

    std::cout << "-- " << text(lang, group.zh_title, group.en_title) << " --\n\n";

    constexpr int kCols = 3;
    constexpr int kColWidth = 26;

    const size_t n = group.items.size();
    const size_t rows = (n + kCols - 1) / kCols;
    for (size_t r = 0; r < rows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            const size_t idx = r + c * rows;
            if (idx < n) {
                const auto& it = group.items[idx];
                std::cout << fmt_item(lang, it.id, it.zh.c_str(), it.en.c_str(), kColWidth);
            }
        }
        std::cout << "\n";
    }
    std::cout << "\n";
    std::cout << " 0." << text(lang, "返回上级", "Back") << "\n\n";

    print_message_area(lang, last_output);

    std::cout << text(lang, "请选择: ", "Choose: ");
}

// 构建一级分类列表
std::vector<TopCatItem> make_top_categories(Language /*lang*/, bool logged_in, bool /*in_battle*/)
{
    std::vector<TopCatItem> cats;
    int id = 0;

    if (!logged_in) {
        // 未登录：只显示登录
        cats.push_back({ ++id, "登录", "Login" });
        return cats;
    }

    // 已登录：PVE/PVP 在一级菜单
    cats.push_back({ ++id, "PVE", "PVE" });
    cats.push_back({ ++id, "PVP", "PVP" });
    cats.push_back({ ++id, "背包商店", "Bag/Shop" });
    cats.push_back({ ++id, "任务", "Quest" });
    cats.push_back({ ++id, "信息", "Info" });
    cats.push_back({ ++id, "系统", "System" });
    return cats;
}

// 构建一级分类对应的二级子菜单
CatGroup make_sub_group(Language lang, int top_id, const std::string& current_player,
                        bool logged_in, bool /*in_battle*/)
{
    int next_id = 0;
    auto next = [&next_id] { return ++next_id; };

    if (!logged_in) {
        // 未登录：只有登录分类
        if (top_id == 1) {
            CatGroup g{ "登录", "Login" };
            g.items.push_back({ next(), "登录玩家", "Login", [lang] {
                                   const std::string name = ask(lang, "玩家名: ", "Player name: ");
                                   return name.empty() ? "" : "login " + name;
                               } });
            g.items.push_back({ next(), "查看帮助", "Help", [] { return "help"; } });
            return g;
        }
        return CatGroup{ "", "" };
    }

    // === 已登录 ===
    if (top_id == 1) {
        // PVE
        CatGroup g{ "PVE", "PVE" };
        g.items.push_back({ next(), "开始PVE(史莱姆)", "Start PVE (Slime)", [lang, current_player] {
                               return with_player("pve", current_player) + " slime";
                           } });
        g.items.push_back(
          { next(), "开始PVE(自定义)", "Start PVE (Custom)", [lang, current_player] {
               const std::string enc = ask(lang, "遭遇id: ", "Encounter id: ");
               return enc.empty() ? "" : append_arg(with_player("pve", current_player), enc);
           } });
        return g;
    }
    if (top_id == 2) {
        // PVP
        CatGroup g{ "PVP", "PVP" };
        g.items.push_back({ next(), "PVP排队", "PVP Queue",
                            [current_player] { return with_player("queue", current_player); } });
        return g;
    }
    if (top_id == 3) {
        CatGroup g{ "背包商店", "Bag/Shop" };
        g.items.push_back({ next(), "查看背包", "Inventory", [current_player] {
                               return with_player("inventory", current_player);
                           } });
        g.items.push_back({ next(), "查看商店", "Shop", [] { return "shop"; } });
        g.items.push_back({ next(), "购买道具", "Buy", [lang, current_player] {
                               const std::string it = ask(lang, "道具id: ", "Item id: ");
                               if (it.empty()) return std::string();
                               const std::string n =
                                 ask(lang, "数量(空=1): ", "Amount (empty=1): ");
                               return append_arg(with_player("buy", current_player) + " " + it, n);
                           } });
        return g;
    }
    if (top_id == 4) {
        CatGroup g{ "任务", "Quest" };
        g.items.push_back({ next(), "可接任务", "Templates", [] { return "quests"; } });
        g.items.push_back({ next(), "接取任务", "Accept", [lang, current_player] {
                               const std::string q = ask(lang, "任务id: ", "Quest id: ");
                               return q.empty() ? "" : "quest accept " + current_player + " " + q;
                           } });
        g.items.push_back({ next(), "我的任务", "My quests", [current_player] {
                               return with_player("quest list", current_player);
                           } });
        g.items.push_back({ next(), "领取奖励", "Claim", [lang, current_player] {
                               const std::string q = ask(lang, "任务id: ", "Quest id: ");
                               return q.empty() ? "" : "quest claim " + current_player + " " + q;
                           } });
        return g;
    }
    if (top_id == 5) {
        CatGroup g{ "信息", "Info" };
        g.items.push_back({ next(), "玩家列表", "Players", [] { return "players"; } });
        g.items.push_back({ next(), "怪物列表", "Monsters", [] { return "monsters"; } });
        g.items.push_back({ next(), "道具列表", "Items", [] { return "items"; } });
        g.items.push_back({ next(), "技能列表", "Skills", [] { return "skills"; } });
        g.items.push_back({ next(), "服务端状态", "State", [] { return "state"; } });
        g.items.push_back({ next(), "战斗日志", "Log", [lang] {
                               const std::string b =
                                 ask(lang, "战斗id(空=最近): ", "Battle id (empty=latest): ");
                               return append_arg("log", b);
                           } });
        return g;
    }
    if (top_id == 6) {
        CatGroup g{ "系统", "System" };
        g.items.push_back({ next(), "保存存档", "Save", [] { return "save"; } });
        g.items.push_back({ next(), "读取存档", "Load", [] { return "load"; } });
        g.items.push_back(
          { next(), "手动命令", "Manual", [lang] { return ask(lang, "命令: ", "Command: "); } });
        g.items.push_back({ next(), "登出", "Logout",
                            [current_player] { return with_player("logout", current_player); } });
        return g;
    }
    return CatGroup{ "", "" };
}

const TopCatItem* find_top_cat(const std::vector<TopCatItem>& cats, int id)
{
    for (const auto& c : cats) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

const CatItem* find_sub_item(const CatGroup& group, int id)
{
    for (const auto& it : group.items) {
        if (it.id == id) return &it;
    }
    return nullptr;
}

// 检测玩家是否在战斗中
bool detect_in_battle(const std::string& output)
{
    // 看到 HP 状态行就认为在战斗中（Battle starts 消息和 HP 状态通常同时出现）
    if (output.find("HP=") != std::string::npos || output.find("hp=") != std::string::npos) {
        return true;
    }
    // 其他战斗特征
    return output.find("Battle") != std::string::npos && output.find("starts") == std::string::npos
           && (output.find("Round ") != std::string::npos
               || output.find("waits") != std::string::npos
               || output.find("is already in battle") != std::string::npos
               || output.find("action locked") != std::string::npos);
}

bool detect_battle_end(const std::string& output)
{
    // 服务端输出格式: "[battle] xxx wins." 或 "[battle] xxx forfeits the battle."
    return output.find(" wins.") != std::string::npos
           || output.find("forfeits the battle") != std::string::npos;
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

// 战斗中直接显示战斗命令（扁平化，不走两级菜单）
bool handle_battle_turn(Language& language, mm::GameServer& server,
                        const std::string& current_player, std::string& last_output,
                        bool& in_battle)
{
    // 构建战斗命令组
    CatGroup battle_group{ "战斗", "Battle" };
    int next_id = 0;
    auto next = [&next_id] { return ++next_id; };

    battle_group.items.push_back({ next(), "轻击", "Attack", [current_player] {
                                      return with_player("attack", current_player);
                                  } });
    battle_group.items.push_back({ next(), "重击", "Heavy", [current_player] {
                                      return with_player("heavy", current_player);
                                  } });
    battle_group.items.push_back({ next(), "火焰攻击", "Fire", [current_player] {
                                      return with_player("fire", current_player);
                                  } });
    battle_group.items.push_back({ next(), "防御", "Defend", [current_player] {
                                      return with_player("defend", current_player);
                                  } });
    battle_group.items.push_back(
      { next(), "治疗", "Heal", [current_player] { return with_player("heal", current_player); } });
    battle_group.items.push_back({ next(), "使用道具", "Use Item", [language, current_player] {
                                      const std::string it = ask(language, "道具id: ", "Item id: ");
                                      return it.empty()
                                               ? ""
                                               : with_player("use", current_player) + " " + it;
                                  } });
    battle_group.items.push_back({ next(), "放弃战斗", "Forfeit", [current_player] {
                                      return with_player("forfeit", current_player);
                                  } });
    battle_group.items.push_back(
      { next(), "刷新状态", "Refresh", [] { return std::string("state"); } });

    print_sub_menu(language, battle_group, current_player, true, last_output);

    const int choice = read_choice();
    if (choice == 0) {
        last_output.clear();
        return false;  // 返回上级（实际上战斗中不能返回，继续战斗）
    }

    const auto* item = find_sub_item(battle_group, choice);
    if (!item) {
        last_output = text(language, "[错误] 无效选项\n", "[error] invalid choice\n");
        return true;
    }

    const std::string command = trim_copy(item->build_command());
    if (command.empty()) {
        last_output = text(language, "[错误] 命令不能为空\n", "[error] command cannot be empty\n");
        return true;
    }

    const std::string output = server.execute(command);
    last_output = output;

    if (detect_battle_end(output)) {
        in_battle = false;
    }

    return in_battle;
}

int main()
{
    setup_console();

    mm::GameServer server;
    Language language = load_or_choose_language();
    std::string current_player;
    bool in_battle = false;
    std::string last_output;

    while (true) {
        const bool logged_in = !current_player.empty();

        // 战斗中：直接显示战斗命令界面
        if (in_battle) {
            if (!handle_battle_turn(language, server, current_player, last_output, in_battle)) {
                continue;  // 0 返回，忽略（战斗中不能返回）
            }
            continue;
        }

        // === 一级分类菜单 ===
        const auto top_cats = make_top_categories(language, logged_in, in_battle);
        print_top_categories(language, top_cats, current_player, in_battle, last_output);

        const int top_choice = read_choice();
        if (top_choice == 0) {
            const std::string quit_output = server.execute("quit");
            std::cout << quit_output;
            break;
        }

        const auto* top_cat = find_top_cat(top_cats, top_choice);
        if (!top_cat) {
            last_output = text(language, "[错误] 无效选项\n", "[error] invalid choice\n");
            continue;
        }

        // === 二级子菜单 ===
        const auto sub_group =
          make_sub_group(language, top_cat->id, current_player, logged_in, in_battle);
        if (sub_group.items.empty()) {
            last_output = text(language, "[错误] 无可用命令\n", "[error] no commands available\n");
            continue;
        }

        print_sub_menu(language, sub_group, current_player, in_battle, last_output);

        const int sub_choice = read_choice();
        if (sub_choice == 0) {
            last_output.clear();
            continue;
        }

        const auto* item = find_sub_item(sub_group, sub_choice);
        if (!item) {
            last_output = text(language, "[错误] 无效选项\n", "[error] invalid choice\n");
            continue;
        }

        const std::string command = trim_copy(item->build_command());
        if (command == "__lang_toggle__") {
            language = (language == Language::Chinese) ? Language::English : Language::Chinese;
            save_language_preference(language);
            last_output = text(language, "语言已切换。\n", "Language switched.\n");
            continue;
        }
        if (command.empty()) {
            last_output =
              text(language, "[错误] 命令不能为空\n", "[error] command cannot be empty\n");
            continue;
        }

        const std::string output = server.execute(command);
        last_output = output;

        if (command == "quit") {
            break;
        }

        // 跟踪登录/登出状态
        if (current_player.empty()) {
            const std::string login_name = first_arg_after_command(command, "login");
            if (!login_name.empty()
                && output.find("[server] " + login_name + " entered the world")
                     != std::string::npos) {
                current_player = login_name;
            }
        }
        else if (command == "logout " + current_player
                 && output.find("[server] " + current_player + " logged out")
                      != std::string::npos) {
            current_player.clear();
            in_battle = false;
        }

        // 跟踪战斗状态
        if (!in_battle && detect_in_battle(output)) {
            in_battle = true;
        }
    }

    return 0;
}
