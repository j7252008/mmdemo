#include "net/ITransport.h"

#include <atomic>
#include <chrono>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

constexpr unsigned short kPort = 7878;

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
#else
    std::setlocale(LC_ALL, "en_US.UTF-8");
#endif
}

std::string lang_config_path()
{
#ifdef _WIN32
    const char* home = nullptr;
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
    if (home && home[0] != '\0') {
        return std::string(home) + "/.mmdemo_lang";
    }
#else
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::string(home) + "/.mmdemo_lang";
    }
#endif
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

std::string ask_optional(Language language, const char* zh, const char* en)
{
    return ask(language, zh, en);
}

std::string append_arg(std::string command, const std::string& arg)
{
    if (!arg.empty()) {
        command += " " + arg;
    }
    return command;
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

// 格式化一个菜单项为固定宽度字符串
std::string fmt_item(Language lang, int id, const char* zh, const char* en, int width)
{
    std::string s = " " + std::to_string(id) + "." + text(lang, zh, en);
    if (static_cast<int>(s.size()) < width) {
        s.append(width - s.size(), ' ');
    }
    return s;
}

void print_top_bar(Language lang, bool logged_in, bool in_battle)
{
    std::cout << "==== " << text(lang, "MMDemo", "MMDemo") << " ====";
    std::cout << "  "
              << text(lang, logged_in ? "已登录" : "未登录",
                      logged_in ? "logged in" : "not logged in");
    if (in_battle) {
        std::cout << "  [" << text(lang, "战斗中", "In Battle") << "]";
    }
    std::cout << "\n";
}

// 将服务端标签 [server][battle][state][error] 等翻译为当前语言
std::string translate_tags(Language lang, const std::string& text)
{
    if (text.empty()) return text;

    // 标签映射表
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
                pos += 2 + std::strlen(tp.zh_tag);  // 跳过已替换的部分
            }
        }
        return result;
    }
    return text;  // 英文保持原样
}

// 完整翻译服务端消息（替换英文短语为中文）。HP/MP 等数值标识保留原样。
std::string translate_message(Language lang, const std::string& text)
{
    if (text.empty() || lang != Language::Chinese) return text;

    // 先翻译标签前缀
    std::string result = translate_tags(lang, text);

    // 翻译映射：{英文片段, 中文替换}。按从长到短排列避免部分匹配。
    struct MsgPair
    {
        const char* en;
        const char* zh;
    };
    static const MsgPair msgs[] = {
        // -- Battle.cpp 输出 --
        { "Battle ", "\u6218\u6597 " },                     // 战斗
        { " starts: ", " \u5f00\u59cb: " },                 // 开始:
        { " vs ", " \u5bf9\u6218 " },                       // 对战
        { "Round ", "\u7b2c " },                            // 第
        { " begins.", " \u56de\u5408\u5f00\u59cb\u3002" },  // 回合开始。
        { " uses ", " \u5bf9 " },                           // 对 (用于"xx uses skill on yy")
        { " on ", " \u4f7f\u7528 " },                       // 使用 (用于"uses xx on yy")
        { " damage.", " \u70b9\u4f24\u5bb3\u3002" },        // 点伤害。
        { " falls.", " \u5012\u4e0b\u4e86\u3002" },         // 倒下了。
        { " wins.", " \u83b7\u80dc\u3002" },                // 获胜。
        { " forfeits the battle.", " \u653e\u5f03\u4e86\u6218\u6597\u3002" },      // 放弃了战斗。
        { " braces for impact.", " \u505a\u597d\u9632\u5fa1\u51c6\u5907\u3002" },  // 做好防御准备。
        { " tried ", " \u8bd5\u56fe\u4f7f\u7528 " },                               // 试图使用
        { " but lacked MP, using Attack.",
          " \u4f46 MP \u4e0d\u8db3\uff0c\u6539\u7528 \u653b\u51fb\u3002" },  // 但 MP 不足，改用
                                                                             // 攻击。
        { " +", " +" },                                                      // HP/MP 增量保持
        { " HP.", " HP\u3002" },                                             // HP。
        { "action locked: ", "\u884c\u52a8\u9501\u5b9a: " },                 // 行动锁定:
        { " -> ", " -> " },                                                  // 保持箭头
        { " / ", " / " },                                                    // 保持斜杠

        // -- GameServer.cpp 输出 --
        // 服务器消息
        { "goodbye", "\u518d\u89c1" },  // 再见
        { "unknown command, type help", "\u672a\u77e5\u547d\u4ee4\uff0c\u8f93\u5165 help "
                                        "\u67e5\u770b\u5e2e\u52a9" },    // 未知命令，输入 help
                                                                         // 查看帮助
        { "name is required", "\u9700\u8981\u63d0\u4f9b\u540d\u79f0" },  // 需要提供名称
        { " is already online", " \u5df2\u7ecf\u5728\u7ebf" },           // 已经在线
        { " reconnected", " \u91cd\u65b0\u8fde\u63a5" },                 // 重新连接
        { " entered the world", " \u8fdb\u5165\u4e86\u4e16\u754c" },     // 进入了世界
        { " is in battle, cannot logout",
          " \u5728\u6218\u6597\u4e2d\uff0c\u65e0\u6cd5\u767b\u51fa" },         // 在战斗中，无法登出
        { " logged out", " \u5df2\u767b\u51fa" },                              // 已登出
        { " is not in battle", " \u4e0d\u5728\u6218\u6597\u4e2d" },            // 不在战斗中
        { "battle disappeared", "\u6218\u6597\u5df2\u6d88\u5931" },            // 战斗已消失
        { " is already in battle", " \u5df2\u7ecf\u5728\u6218\u6597\u4e2d" },  // 已经在战斗中
        { "unknown encounter: ", "\u672a\u77e5\u906d\u9047: " },               // 未知遭遇:
        { " references unknown monster: ",
          " \u5f15\u7528\u4e86\u672a\u77e5\u602a\u7269: " },                  // 引用了未知怪物:
        { "encounter ", "\u906d\u9047 " },                                    // 遭遇
        { " enters ", " \u8fdb\u5165\u4e86 " },                               // 进入了
        { " is already queued", " \u5df2\u7ecf\u5728\u6392\u961f" },          // 已经在排队
        { " queued", " \u5df2\u6392\u961f" },                                 // 已排队
        { "item id is required", "\u9700\u8981\u63d0\u4f9b\u9053\u5177ID" },  // 需要提供道具ID
        { "unknown item: ", "\u672a\u77e5\u9053\u5177: " },                   // 未知道具:
        { " does not have ", " \u6ca1\u6709 " },                              // 没有
        { "player name is required",
          "\u9700\u8981\u63d0\u4f9b\u73a9\u5bb6\u540d" },           // 需要提供玩家名
        { "unknown player: ", "\u672a\u77e5\u73a9\u5bb6: " },       // 未知玩家:
        { "no players", "\u65e0\u73a9\u5bb6" },                     // 无玩家
        { " receives ", " \u83b7\u5f97\u4e86 " },                   // 获得了
        { " exp and ", " \u7ecf\u9a8c\u548c " },                    // 经验和
        { " gold", " \u91d1\u5e01" },                               // 金币
        { ". Level up to ", "\u3002\u5347\u7ea7\u5230 " },          // 。升级到
        { "!", "\u7ea7\uff01" },                                    // 级！
        { " receives item ", " \u83b7\u5f97\u4e86\u9053\u5177 " },  // 获得了道具
        { " progress ", " \u8fdb\u5ea6 " },                         // 进度
        { " bought ", " \u8d2d\u4e70\u4e86 " },                     // 购买了
        { " for ", " \uff0c\u82b1\u8d39 " },                        // ，花费
        { " lacks gold. need=",
          " \u91d1\u5e01\u4e0d\u8db3\u3002\u9700\u8981=" },           // 金币不足。需要=
        { " have=", " \u6301\u6709=" },                               // 持有=
        { " accepted ", " \u63a5\u53d7\u4e86 " },                     // 接受了
        { " already has ", " \u5df2\u7ecf\u62e5\u6709 " },            // 已经拥有
        { " has no quests", " \u6ca1\u6709\u4efb\u52a1" },            // 没有任务
        { " claimed ", " \u9886\u53d6\u4e86 " },                      // 领取了
        { " reward ", " \u5956\u52b1 " },                             // 奖励
        { " has not accepted ", " \u5c1a\u672a\u63a5\u53d7 " },       // 尚未接受
        { " already claimed ", " \u5df2\u7ecf\u9886\u53d6\u4e86 " },  // 已经领取了
        { "saved ", "\u5df2\u4fdd\u5b58 " },                          // 已保存
        { " players to ", " \u4e2a\u73a9\u5bb6\u5230 " },             // 个玩家到
        { "loaded ", "\u4ece " },                                     // 从
        { " players from ", " \u52a0\u8f7d\u4e86 " },                 // 加载了
        { "cannot open ", "\u65e0\u6cd5\u6253\u5f00 " },              // 无法打开
        { "invalid player json: ",
          "\u65e0\u6548\u7684\u73a9\u5bb6\u6570\u636e: " },    // 无效的玩家数据:
        { "unknown battle: ", "\u672a\u77e5\u6218\u6597: " },  // 未知战斗:
        { "unknown quest: ", "\u672a\u77e5\u4efb\u52a1: " },   // 未知任务:
        { "quest command: accept/list/claim",
          "\u4efb\u52a1\u547d\u4ee4: accept/list/claim" },  // 任务命令:

        // 玩家列表关键词
        { " online", " \u5728\u7ebf" },                      // 在线
        { " offline", " \u79bb\u7ebf" },                     // 离线
        { " battle=", " \u6218\u6597=" },                    // 战斗=
        { " inventory=", " \u80cc\u5305=" },                 // 背包=
        { " quests=", " \u4efb\u52a1=" },                    // 任务=
        { "queue:", "\u6392\u961f:" },                       // 排队:
        { "active battles:", "\u6d3b\u8dc3\u6218\u6597:" },  // 活跃战斗:

        // 帮助文本 (每行单独匹配)
        { "commands:", "\u547d\u4ee4:" },                                       // 命令:
        { "create a player session", "\u521b\u5efa\u73a9\u5bb6\u4f1a\u8bdd" },  // 创建玩家会话
        { "mark player offline when not in battle",
          "\u5c06\u73a9\u5bb6\u6807\u8bb0\u4e3a\u79bb\u7ebf\uff08\u975e\u6218\u6597\u4e2d"
          "\uff09" },  // 将玩家标记为离线（非战斗中）
        { "concede current battle", "\u653e\u5f03\u5f53\u524d\u6218\u6597" },  // 放弃当前战斗
        { "start PVE: slime / fox / bandit / forest / camp",
          "\u5f00\u59cb PVE: slime / fox / bandit / forest / camp" },
        { "enter 1v1 matchmaking", "\u8fdb\u5165 1v1 \u5339\u914d" },          // 进入 1v1 匹配
        { "normal attack", "\u666e\u901a\u653b\u51fb" },                       // 普通攻击
        { "heavy strike, costs 8 MP", "\u91cd\u51fb\uff0c\u6d88\u80178 MP" },  // 重击，消耗8 MP
        { "alias of heavy", "\u91cd\u51fb\u7684\u522b\u540d" },                // 重击的别名
        { "fire charm, costs 10 MP",
          "\u706b\u7130\u672f\uff0c\u6d88\u801710 MP" },  // 火焰术，消耗10 MP
        { "reduce incoming damage this round", "\u672c\u56de\u5408\u51cf\u5c11\u53d7\u5230\u7684"
                                               "\u4f24\u5bb3" },  // 本回合减少受到的伤害
        { "heal self or ally, costs 6 MP", "\u6cbb\u7597\u81ea\u5df1\u6216\u961f\u53cb\uff0c\u6d88"
                                           "\u80176 MP" },  // 治疗自己或队友，消耗6
                                                            // MP
        { "use a battle item, e.g. use alice potion",
          "\u4f7f\u7528\u6218\u6597\u9053\u5177\uff0c\u4f8b\u5982 use alice potion" },
        { "list player inventory", "\u67e5\u770b\u73a9\u5bb6\u80cc\u5305" },  // 查看玩家背包
        { "debug grant item",
          "\u8c03\u8bd5\u7528\uff1a\u8d60\u4e0e\u9053\u5177" },         // 调试用：赠予道具
        { "list shop items", "\u67e5\u770b\u5546\u5e97\u9053\u5177" },  // 查看商店道具
        { "buy items with gold", "\u7528\u91d1\u5e01\u8d2d\u4e70\u9053\u5177" },  // 用金币购买道具
        { "list quest templates", "\u67e5\u770b\u4efb\u52a1\u6a21\u677f" },       // 查看任务模板
        { "accept quest", "\u63a5\u53d7\u4efb\u52a1" },                           // 接受任务
        { "list player quests", "\u67e5\u770b\u73a9\u5bb6\u4efb\u52a1" },         // 查看玩家任务
        { "claim completed quest",
          "\u9886\u53d6\u5df2\u5b8c\u6210\u4efb\u52a1\u5956\u52b1" },          // 领取已完成任务奖励
        { "list player profiles", "\u67e5\u770b\u73a9\u5bb6\u4fe1\u606f" },    // 查看玩家信息
        { "list monster templates", "\u67e5\u770b\u602a\u7269\u6a21\u677f" },  // 查看怪物模板
        { "list item templates", "\u67e5\u770b\u9053\u5177\u6a21\u677f" },     // 查看道具模板
        { "list skill templates", "\u67e5\u770b\u6280\u80fd\u6a21\u677f" },    // 查看技能模板
        { "print server state", "\u6253\u5370\u670d\u52a1\u5668\u72b6\u6001" },  // 打印服务器状态
        { "print latest or selected battle log",
          "\u6253\u5370\u6700\u8fd1\u6216\u6307\u5b9a"
          "\u6218\u6597\u65e5\u5fd7" },  // 打印最近或指定战斗日志
        { "save players to data/players.json", "\u4fdd\u5b58\u73a9\u5bb6\u5230 data/players.json" },
        { "load players from data/players.json",
          "\u4ece data/players.json \u52a0\u8f7d\u73a9\u5bb6" },
        { "show commands", "\u663e\u793a\u547d\u4ee4" },  // 显示命令
        { "exit", "\u9000\u51fa" },                       // 退出
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

    // 提取非状态行（[state] 行已在战斗状态区域显示，消息区域不再重复）
    std::vector<std::string> other_lines;
    {
        std::istringstream iss(translated);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            if (line.find("[state]") != std::string::npos
                || line.find("\u72b6\u6001") != std::string::npos) {  // 状态
                continue;  // 跳过，战斗状态区域已单独显示
            }
            else {
                other_lines.push_back(line);
            }
        }
    }

    std::cout << "----------------------------------------\n";
    std::cout << text(lang, "[消息]", "[Messages]") << "\n";

    // 其余消息
    for (const auto& o : other_lines) {
        std::cout << o << "\n";
    }

    std::cout << "----------------------------------------\n";
}

// 一级分类
struct TopCatItem
{
    int id;
    std::string zh;
    std::string en;
};

void print_top_categories(Language lang, const std::vector<TopCatItem>& cats, bool logged_in,
                          bool in_battle, const std::string& last_output)
{
    clear_screen();

    print_top_bar(lang, logged_in, in_battle);

    // 战斗中：直接在顶栏下方显示 [state] 行
    if (in_battle) {
        std::istringstream iss(last_output);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.find("[state]") != std::string::npos
                || line.find("\u72b6\u6001") != std::string::npos
                || line.find("HP ") != std::string::npos || line.find("hp ") != std::string::npos) {
                std::cout << line << "\n";
            }
        }
    }

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

    std::cout << " 0." << text(lang, "退出", "Quit");
    if (in_battle) {
        std::cout << "  99." << text(lang, "刷新状态", "Refresh");
    }
    std::cout << "\n\n";

    print_message_area(lang, last_output);

    std::cout << text(lang, "请选择: ", "Choose: ");
}

void print_sub_menu(Language lang, const CatGroup& group, bool logged_in, bool in_battle,
                    const std::string& last_output)
{
    clear_screen();

    print_top_bar(lang, logged_in, in_battle);

    // 战斗中：直接在顶栏下方显示 [state] 行
    if (in_battle) {
        std::istringstream iss(last_output);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.find("[state]") != std::string::npos
                || line.find("\u72b6\u6001") != std::string::npos
                || line.find("HP ") != std::string::npos || line.find("hp ") != std::string::npos) {
                std::cout << line << "\n\n";
            }
        }
    }

    // 战斗命令
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
    std::cout << " 0." << text(lang, "返回上级", "Back") << "\n\n";

    print_message_area(lang, last_output);

    std::cout << text(lang, "请选择: ", "Choose: ");
}

std::vector<TopCatItem> make_top_categories(Language /*lang*/, bool logged_in, bool /*in_battle*/)
{
    std::vector<TopCatItem> cats;
    int id = 0;

    if (!logged_in) {
        // 未登录：只显示登录
        cats.push_back({ ++id, "登录", "Login" });
        return cats;
    }

    cats.push_back({ ++id, "PVE", "PVE" });
    cats.push_back({ ++id, "PVP", "PVP" });
    cats.push_back({ ++id, "背包商店", "Bag/Shop" });
    cats.push_back({ ++id, "任务", "Quest" });
    cats.push_back({ ++id, "信息", "Info" });
    cats.push_back({ ++id, "系统", "System" });
    return cats;
}

CatGroup make_sub_group(Language lang, int top_id, bool logged_in, bool /*in_battle*/)
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
        g.items.push_back({ next(), "开始PVE(史莱姆)", "Start PVE (Slime)",
                            [] { return std::string("pve slime"); } });
        g.items.push_back({ next(), "开始PVE(自定义)", "Start PVE (Custom)", [lang] {
                               const std::string enc = ask(lang, "遭遇id: ", "Encounter id: ");
                               return enc.empty() ? "" : append_arg("pve", enc);
                           } });
        return g;
    }
    if (top_id == 2) {
        // PVP
        CatGroup g{ "PVP", "PVP" };
        g.items.push_back({ next(), "PVP排队", "PVP Queue", [] { return "queue"; } });
        return g;
    }
    if (top_id == 3) {
        CatGroup g{ "背包商店", "Bag/Shop" };
        g.items.push_back({ next(), "查看背包", "Inventory", [] { return "inventory"; } });
        g.items.push_back({ next(), "查看商店", "Shop", [] { return "shop"; } });
        g.items.push_back({ next(), "购买道具", "Buy", [lang] {
                               const std::string it = ask(lang, "道具id: ", "Item id: ");
                               if (it.empty()) return std::string();
                               const std::string n =
                                 ask_optional(lang, "数量(空=1): ", "Amount (empty=1): ");
                               return append_arg("buy " + it, n);
                           } });
        return g;
    }
    if (top_id == 4) {
        CatGroup g{ "任务", "Quest" };
        g.items.push_back({ next(), "可接任务", "Templates", [] { return "quests"; } });
        g.items.push_back({ next(), "接取任务", "Accept", [lang] {
                               const std::string q = ask(lang, "任务id: ", "Quest id: ");
                               return q.empty() ? "" : "quest accept " + q;
                           } });
        g.items.push_back({ next(), "我的任务", "My quests", [] { return "quest list"; } });
        g.items.push_back({ next(), "领取奖励", "Claim", [lang] {
                               const std::string q = ask(lang, "任务id: ", "Quest id: ");
                               return q.empty() ? "" : "quest claim " + q;
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
                               const std::string b = ask_optional(
                                 lang, "战斗id(空=最近): ", "Battle id (empty=latest): ");
                               return append_arg("log", b);
                           } });
        return g;
    }
    if (top_id == 6) {
        CatGroup g{ "系统", "System" };
        g.items.push_back({ next(), "保存存档", "Save", [] { return "save"; } });
        g.items.push_back({ next(), "读取存档", "Load", [] { return "load"; } });
        g.items.push_back({ next(), "登出", "Logout", [] { return "logout"; } });
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

// 战斗中直接显示战斗命令（扁平化，不走两级菜单）
// transport: network transport (replaces raw SOCKET).
bool handle_battle_turn(Language& language, mm::net::IClientTransport& transport,
                        std::string& last_output, std::mutex& output_mutex,
                        std::atomic<bool>& running, bool& in_battle)
{
    CatGroup battle_group{ "战斗", "Battle" };
    int next_id = 0;
    auto next = [&next_id] { return ++next_id; };

    battle_group.items.push_back({ next(), "轻击", "Attack", [] { return "attack"; } });
    battle_group.items.push_back({ next(), "重击", "Heavy", [] { return "heavy"; } });
    battle_group.items.push_back({ next(), "火焰攻击", "Fire", [] { return "fire"; } });
    battle_group.items.push_back({ next(), "防御", "Defend", [] { return "defend"; } });
    battle_group.items.push_back({ next(), "治疗", "Heal", [] { return "heal"; } });
    battle_group.items.push_back({ next(), "使用道具", "Use Item", [language] {
                                      const std::string it = ask(language, "道具id: ", "Item id: ");
                                      return it.empty() ? "" : "use " + it;
                                  } });
    battle_group.items.push_back({ next(), "放弃战斗", "Forfeit", [] { return "forfeit"; } });
    battle_group.items.push_back(
      { next(), "刷新状态", "Refresh", [] { return std::string("state"); } });

    // 加锁安全读取 last_output
    std::string output_copy;
    {
        std::lock_guard<std::mutex> lock(output_mutex);
        output_copy = last_output;
    }
    print_sub_menu(language, battle_group, true, true, output_copy);

    const int choice = read_choice();
    if (choice == 0) {
        std::lock_guard<std::mutex> lock(output_mutex);
        last_output.clear();
        return false;
    }

    const auto* item = find_sub_item(battle_group, choice);
    if (!item) {
        std::lock_guard<std::mutex> lock(output_mutex);
        last_output = text(language, "无效选择。\n", "Invalid choice.\n");
        return true;
    }

    std::string command = item->build_command();
    command = trim_copy(command);
    if (command.empty()) {
        std::lock_guard<std::mutex> lock(output_mutex);
        last_output = text(language, "命令已取消。\n", "Command canceled.\n");
        return true;
    }

    // 发送前先清空 last_output，以便用"是否收到新数据"来判断响应已到达
    {
        std::lock_guard<std::mutex> lock(output_mutex);
        last_output.clear();
    }

    if (!transport.send(command + "\n")) {
        return false;
    }

    // 轮询等待 receive_loop 写入新数据，最多等 3 秒
    std::string response;
    for (int i = 0; i < 60 && running.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        {
            std::lock_guard<std::mutex> lock(output_mutex);
            if (!last_output.empty()) {
                response = last_output;
                break;
            }
        }
    }

    // 如果没收到响应，保持当前状态
    if (response.empty()) {
        return in_battle;
    }

    // 检测战斗结束（服务端输出格式: "[battle] xxx wins."）
    if (command == "forfeit" || response.find(" wins.") != std::string::npos
        || response.find("forfeits the battle") != std::string::npos) {
        in_battle = false;
        // 保留战斗结束消息在 last_output 中，让用户看到结果
        return false;
    }

    return in_battle;
}

}  // namespace

int main()
{
    try {
        setup_console();
        Language language = load_or_choose_language();

        // ---- Choose transport backend here ----
        // To switch to another network library, just change the enum value.
        auto transport = mm::net::create_client_transport(
            mm::net::TransportBackend::Asio);
        if (!transport) {
            std::cerr << "failed to create client transport\n";
            return 1;
        }

        if (!transport->connect("127.0.0.1", kPort)) {
            std::cerr << text(language, "连接失败，请先启动 mmdemo_server。\n",
                              "connect failed. Start mmdemo_server first.\n");
            return 1;
        }

        std::cout << text(
          language, "已连接到 127.0.0.1:7878。服务端响应仍以原始战报文本显示。\n",
          "Connected to 127.0.0.1:7878. Server responses are shown as raw battle text.\n");

        std::atomic<bool> running{ true };
        std::mutex output_mutex;
        std::string last_output;

        // Wire transport callbacks.
        transport->set_on_data([&](const std::string& data) {
            std::cout << "\n" << data;
            std::cout.flush();
            {
                std::lock_guard<std::mutex> lock(output_mutex);
                last_output = data;
            }
        });

        transport->set_on_disconnected([&]() {
            running.store(false);
        });

        bool logged_in = false;
        bool in_battle = false;

        while (running.load()) {
            // 战斗中：直接显示战斗命令界面
            if (in_battle) {
                if (!handle_battle_turn(language, *transport, last_output, output_mutex, running,
                                        in_battle)) {
                    continue;
                }
                continue;
            }

            // === 一级分类菜单 ===
            const auto top_cats = make_top_categories(language, logged_in, in_battle);
            {
                std::lock_guard<std::mutex> lock(output_mutex);
                print_top_categories(language, top_cats, logged_in, in_battle, last_output);
            }

            const int top_choice = read_choice();
            if (top_choice == 0) {
                transport->send("quit\n");
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                break;
            }

            const auto* top_cat = find_top_cat(top_cats, top_choice);
            if (!top_cat) {
                std::lock_guard<std::mutex> lock(output_mutex);
                last_output = text(language, "无效选择。\n", "Invalid choice.\n");
                continue;
            }

            // === 二级子菜单 ===
            const auto sub_group = make_sub_group(language, top_cat->id, logged_in, in_battle);
            if (sub_group.items.empty()) {
                std::lock_guard<std::mutex> lock(output_mutex);
                last_output = text(language, "无可用命令。\n", "No commands available.\n");
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(output_mutex);
                print_sub_menu(language, sub_group, logged_in, in_battle, last_output);
            }

            const int sub_choice = read_choice();
            if (sub_choice == 0) {
                std::lock_guard<std::mutex> lock(output_mutex);
                last_output.clear();
                continue;
            }

            const auto* item = find_sub_item(sub_group, sub_choice);
            if (!item) {
                std::lock_guard<std::mutex> lock(output_mutex);
                last_output = text(language, "无效选择。\n", "Invalid choice.\n");
                continue;
            }

            std::string line = item->build_command();
            line = trim_copy(line);
            if (line == "__lang_toggle__") {
                language = (language == Language::Chinese) ? Language::English : Language::Chinese;
                save_language_preference(language);
                std::lock_guard<std::mutex> lock(output_mutex);
                last_output = text(language, "语言已切换。\n", "Language switched.\n");
                continue;
            }
            if (line.empty()) {
                std::lock_guard<std::mutex> lock(output_mutex);
                last_output = text(language, "命令已取消。\n", "Command canceled.\n");
                continue;
            }

            if (!transport->send(line + "\n")) {
                break;
            }

            // 等待服务端响应
            std::this_thread::sleep_for(std::chrono::milliseconds(300));

            if (line.rfind("login ", 0) == 0) {
                logged_in = true;
            }
            else if (line == "logout") {
                logged_in = false;
                in_battle = false;
            }
            else if (line == "quit" || line == "exit") {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                break;
            }

            // 跟踪战斗状态 (net_client 通过命令判断)
            if (line == "pve slime" || line.rfind("pve ", 0) == 0 || line == "queue") {
                in_battle = true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        transport->disconnect();
    }
    catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << "\n";
        return 1;
    }
}
