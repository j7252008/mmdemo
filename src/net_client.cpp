#ifndef _WIN32
#error "This prototype TCP client uses Winsock and currently targets Windows."
#endif

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr unsigned short kPort = 7878;

enum class Language {
    Chinese,
    English,
};

class WinsockRuntime
{
public:
    WinsockRuntime()
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }

    ~WinsockRuntime() { WSACleanup(); }
};

const char* text(Language language, const char* zh, const char* en)
{
    return language == Language::Chinese ? zh : en;
}

void setup_console()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

std::string lang_config_path()
{
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

bool send_all(SOCKET socket, const std::string& text)
{
    size_t sent = 0;
    while (sent < text.size()) {
        const int chunk = send(socket, text.data() + sent, static_cast<int>(text.size() - sent), 0);
        if (chunk == SOCKET_ERROR || chunk == 0) {
            return false;
        }
        sent += static_cast<size_t>(chunk);
    }
    return true;
}

void receive_loop(SOCKET socket, std::atomic<bool>& running)
{
    char buffer[1024];
    while (running.load()) {
        const int received = recv(socket, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            running.store(false);
            break;
        }
        buffer[received] = '\0';
        std::cout << "\n" << buffer;
        std::cout.flush();
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
    bool requires_login = false;
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
    system("cls");
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

void print_categories(Language lang, const std::vector<CatGroup>& groups, bool logged_in)
{
    clear_screen();
    std::cout << "==== " << text(lang, "可用命令", "Available Commands") << " ====";
    std::cout << "  "
              << text(lang, logged_in ? "已登录" : "未登录",
                      logged_in ? "logged in" : "not logged in")
              << "\n\n";

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

std::vector<CatGroup> make_categories(Language language, bool logged_in,
                                      std::function<void()> toggle_lang_callback = {})
{
    int next_id = 0;
    auto next = [&next_id] { return ++next_id; };

    if (!logged_in) {
        std::vector<CatGroup> groups;
        {
            CatGroup g{ "账号", "Account" };
            g.items.push_back({ next(), "登录玩家", "Login player", false, [language] {
                                   const std::string name =
                                     ask(language, "玩家名: ", "Player name: ");
                                   return name.empty() ? "" : "login " + name;
                               } });
            g.items.push_back({ next(), "查看帮助", "Show help", false, [] { return "help"; } });
            groups.push_back(std::move(g));
        }
        {
            CatGroup g{ "信息查询", "Info" };
            g.items.push_back({ next(), "查看怪物", "Monsters", false, [] { return "monsters"; } });
            g.items.push_back({ next(), "查看道具", "Items", false, [] { return "items"; } });
            g.items.push_back({ next(), "查看技能", "Skills", false, [] { return "skills"; } });
            g.items.push_back({ next(), "查看任务", "Quests", false, [] { return "quests"; } });
            g.items.push_back({ next(), "查看商店", "Shop", false, [] { return "shop"; } });
            groups.push_back(std::move(g));
        }
        {
            CatGroup g{ "系统", "System" };
            g.items.push_back({ next(), "读取存档", "Load", false, [] { return "load"; } });
            g.items.push_back({ next(), "切换语言", "Switch lang", false, [toggle_lang_callback] {
                                   if (toggle_lang_callback) toggle_lang_callback();
                                   return std::string("__lang_toggle__");
                               } });
            g.items.push_back({ next(), "退出", "Quit", false, [] { return "quit"; } });
            groups.push_back(std::move(g));
        }
        return groups;
    }

    // 已登录
    std::vector<CatGroup> groups;
    {
        CatGroup g{ "战斗", "Battle" };
        g.items.push_back({ next(), "开始PVE", "Start PVE", true, [language] {
                               const std::string enc = ask_optional(
                                 language, "遭遇id(空=slime): ", "Encounter (empty=slime): ");
                               return append_arg("pve", enc);
                           } });
        g.items.push_back({ next(), "PVP排队", "PVP Queue", true, [] { return "queue"; } });
        g.items.push_back({ next(), "攻击", "Attack", true, [language] {
                               const std::string t =
                                 ask_optional(language, "目标(空=auto): ", "Target (empty=auto): ");
                               return append_arg("attack", t);
                           } });
        g.items.push_back({ next(), "重击", "Heavy", true, [language] {
                               const std::string t =
                                 ask_optional(language, "目标(空=auto): ", "Target (empty=auto): ");
                               return append_arg("heavy", t);
                           } });
        g.items.push_back({ next(), "火符", "Fire", true, [language] {
                               const std::string t =
                                 ask_optional(language, "目标(空=auto): ", "Target (empty=auto): ");
                               return append_arg("fire", t);
                           } });
        g.items.push_back({ next(), "防御", "Defend", true, [] { return "defend"; } });
        g.items.push_back({ next(), "治疗", "Heal", true, [language] {
                               const std::string t =
                                 ask_optional(language, "目标(空=self): ", "Target (empty=self): ");
                               return append_arg("heal", t);
                           } });
        g.items.push_back({ next(), "使用道具", "Use Item", true, [language] {
                               const std::string it = ask(language, "道具id: ", "Item id: ");
                               if (it.empty()) return std::string();
                               const std::string t =
                                 ask_optional(language, "目标(空=self): ", "Target (empty=self): ");
                               return append_arg("use " + it, t);
                           } });
        g.items.push_back({ next(), "认输", "Forfeit", true, [] { return "forfeit"; } });
        groups.push_back(std::move(g));
    }
    {
        CatGroup g{ "背包/商店", "Bag/Shop" };
        g.items.push_back({ next(), "查看背包", "Inventory", true, [] { return "inventory"; } });
        g.items.push_back({ next(), "查看商店", "Shop", true, [] { return "shop"; } });
        g.items.push_back({ next(), "购买道具", "Buy item", true, [language] {
                               const std::string it = ask(language, "道具id: ", "Item id: ");
                               if (it.empty()) return std::string();
                               const std::string n =
                                 ask_optional(language, "数量(空=1): ", "Amount (empty=1): ");
                               return append_arg("buy " + it, n);
                           } });
        groups.push_back(std::move(g));
    }
    {
        CatGroup g{ "任务", "Quest" };
        g.items.push_back({ next(), "可接任务", "Templates", true, [] { return "quests"; } });
        g.items.push_back({ next(), "接取任务", "Accept", true, [language] {
                               const std::string q = ask(language, "任务id: ", "Quest id: ");
                               return q.empty() ? "" : "quest accept " + q;
                           } });
        g.items.push_back({ next(), "我的任务", "My quests", true, [] { return "quest list"; } });
        g.items.push_back({ next(), "领取奖励", "Claim", true, [language] {
                               const std::string q = ask(language, "任务id: ", "Quest id: ");
                               return q.empty() ? "" : "quest claim " + q;
                           } });
        groups.push_back(std::move(g));
    }
    {
        CatGroup g{ "信息", "Info" };
        g.items.push_back({ next(), "玩家列表", "Players", true, [] { return "players"; } });
        g.items.push_back({ next(), "怪物列表", "Monsters", true, [] { return "monsters"; } });
        g.items.push_back({ next(), "道具列表", "Items", true, [] { return "items"; } });
        g.items.push_back({ next(), "技能列表", "Skills", true, [] { return "skills"; } });
        g.items.push_back({ next(), "服务端状态", "ServerState", true, [] { return "state"; } });
        g.items.push_back({ next(), "战斗日志", "BattleLog", true, [language] {
                               const std::string b = ask_optional(
                                 language, "战斗id(空=最近): ", "Battle id (empty=latest): ");
                               return append_arg("log", b);
                           } });
        groups.push_back(std::move(g));
    }
    {
        CatGroup g{ "系统", "System" };
        g.items.push_back({ next(), "保存存档", "Save", true, [] { return "save"; } });
        g.items.push_back({ next(), "读取存档", "Load", true, [] { return "load"; } });
        g.items.push_back({ next(), "登出", "Logout", true, [] { return "logout"; } });
        g.items.push_back({ next(), "退出", "Quit", true, [] { return "quit"; } });
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

}  // namespace

int main()
{
    try {
        setup_console();
        Language language = load_or_choose_language();
        WinsockRuntime winsock;

        SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_handle == INVALID_SOCKET) {
            std::cerr << text(language, "socket 创建失败\n", "socket failed\n");
            return 1;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(kPort);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (connect(socket_handle, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))
            == SOCKET_ERROR) {
            std::cerr << text(language, "连接失败，请先启动 mmdemo_server。\n",
                              "connect failed. Start mmdemo_server first.\n");
            closesocket(socket_handle);
            return 1;
        }

        std::cout << text(
          language, "已连接到 127.0.0.1:7878。服务端响应仍以原始战报文本显示。\n",
          "Connected to 127.0.0.1:7878. Server responses are shown as raw battle text.\n");

        std::atomic<bool> running{ true };
        std::thread receiver(receive_loop, socket_handle, std::ref(running));

        bool logged_in = false;
        while (running.load()) {
            auto toggle_lang = [&language] {
                language = (language == Language::Chinese) ? Language::English : Language::Chinese;
                save_language_preference(language);
            };
            const auto groups = make_categories(language, logged_in, toggle_lang);
            print_categories(language, groups, logged_in);
            const int choice = read_choice();
            const auto* item = find_item(groups, choice);
            if (!item) {
                std::cout << text(language, "无效选择。\n", "Invalid choice.\n");
                continue;
            }

            std::string line = item->build_command();
            line = trim_copy(line);
            if (line == "__lang_toggle__") {
                std::cout << text(language, "语言已切换。\n", "Language switched.\n");
                continue;
            }
            if (line.empty()) {
                std::cout << text(language, "命令已取消。\n", "Command canceled.\n");
                continue;
            }

            if (!send_all(socket_handle, line + "\n")) {
                break;
            }

            // 等待服务端响应
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            std::cout << text(language, "\n按回车继续...", "\nPress Enter to continue...");
            std::string dummy;
            std::getline(std::cin, dummy);

            if (line.rfind("login ", 0) == 0) {
                logged_in = true;
            }
            else if (line == "logout") {
                logged_in = false;
            }
            else if (line == "quit" || line == "exit") {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        running.store(false);
        shutdown(socket_handle, SD_BOTH);
        closesocket(socket_handle);
        if (receiver.joinable()) {
            receiver.join();
        }
    }
    catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << "\n";
        return 1;
    }
}