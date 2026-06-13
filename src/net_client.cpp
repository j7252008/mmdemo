#ifndef _WIN32
#error "This prototype TCP client uses Winsock and currently targets Windows."
#endif

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
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

struct MenuItem
{
    std::string zh;
    std::string en;
    bool requires_login = false;
    std::function<std::string()> build_command;
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

std::vector<MenuItem> make_menu(Language language, bool logged_in)
{
    (void)language;
    std::vector<MenuItem> items;

    if (!logged_in) {
        items.push_back({ "登录玩家", "Login player", false, [language] {
                             const std::string name = ask(language, "玩家名: ", "Player name: ");
                             return name.empty() ? "" : "login " + name;
                         } });
        items.push_back({ "查看帮助", "Show help", false, [] { return "help"; } });
        items.push_back({ "查看怪物/遭遇", "List monsters/encounters", false, [] { return "monsters"; } });
        items.push_back({ "查看道具", "List items", false, [] { return "items"; } });
        items.push_back({ "查看技能", "List skills", false, [] { return "skills"; } });
        items.push_back({ "查看任务", "List quests", false, [] { return "quests"; } });
        items.push_back({ "查看商店", "List shop", false, [] { return "shop"; } });
        items.push_back({ "读取存档", "Load players", false, [] { return "load"; } });
        items.push_back({ "退出", "Quit", false, [] { return "quit"; } });
        return items;
    }

    items.push_back({ "开始 PVE", "Start PVE", true, [language] {
                         const std::string encounter =
                           ask_optional(language, "遭遇 id，可留空默认 slime: ", "Encounter id, empty for slime: ");
                         return append_arg("pve", encounter);
                     } });
    items.push_back({ "加入 PVP 队列", "Queue for PVP", true, [] { return "queue"; } });
    items.push_back({ "普通攻击", "Attack", true, [language] {
                         const std::string target =
                           ask_optional(language, "目标，可留空自动选择: ", "Target, empty for auto: ");
                         return append_arg("attack", target);
                     } });
    items.push_back({ "重击", "Heavy strike", true, [language] {
                         const std::string target =
                           ask_optional(language, "目标，可留空自动选择: ", "Target, empty for auto: ");
                         return append_arg("heavy", target);
                     } });
    items.push_back({ "火符", "Fire charm", true, [language] {
                         const std::string target =
                           ask_optional(language, "目标，可留空自动选择: ", "Target, empty for auto: ");
                         return append_arg("fire", target);
                     } });
    items.push_back({ "防御", "Defend", true, [] { return "defend"; } });
    items.push_back({ "治疗", "Heal", true, [language] {
                         const std::string target =
                           ask_optional(language, "目标，可留空治疗自己: ", "Target, empty for self: ");
                         return append_arg("heal", target);
                     } });
    items.push_back({ "使用道具", "Use item", true, [language] {
                         const std::string item = ask(language, "道具 id: ", "Item id: ");
                         const std::string target =
                           ask_optional(language, "目标，可留空自己: ", "Target, empty for self: ");
                         return append_arg("use " + item, target);
                     } });
    items.push_back({ "查看背包", "Show inventory", true, [] { return "inventory"; } });
    items.push_back({ "查看商店", "Show shop", true, [] { return "shop"; } });
    items.push_back({ "购买道具", "Buy item", true, [language] {
                         const std::string item = ask(language, "道具 id: ", "Item id: ");
                         const std::string amount = ask_optional(language, "数量，可留空 1: ", "Amount, empty for 1: ");
                         return append_arg("buy " + item, amount);
                     } });
    items.push_back({ "查看可接任务", "List quest templates", true, [] { return "quests"; } });
    items.push_back({ "接取任务", "Accept quest", true, [language] {
                         const std::string quest = ask(language, "任务 id: ", "Quest id: ");
                         return quest.empty() ? "" : "quest accept " + quest;
                     } });
    items.push_back({ "查看我的任务", "List my quests", true, [] { return "quest list"; } });
    items.push_back({ "领取任务奖励", "Claim quest", true, [language] {
                         const std::string quest = ask(language, "任务 id: ", "Quest id: ");
                         return quest.empty() ? "" : "quest claim " + quest;
                     } });
    items.push_back({ "认输", "Forfeit", true, [] { return "forfeit"; } });
    items.push_back({ "查看玩家", "List players", true, [] { return "players"; } });
    items.push_back({ "查看怪物/遭遇", "List monsters/encounters", true, [] { return "monsters"; } });
    items.push_back({ "查看道具", "List items", true, [] { return "items"; } });
    items.push_back({ "查看技能", "List skills", true, [] { return "skills"; } });
    items.push_back({ "查看服务端状态", "Show server state", true, [] { return "state"; } });
    items.push_back({ "查看战斗日志", "Show battle log", true, [language] {
                         const std::string battle =
                           ask_optional(language, "战斗 id，可留空最近一场: ", "Battle id, empty for latest: ");
                         return append_arg("log", battle);
                     } });
    items.push_back({ "保存存档", "Save players", true, [] { return "save"; } });
    items.push_back({ "读取存档", "Load players", true, [] { return "load"; } });
    items.push_back({ "登出", "Logout", true, [] { return "logout"; } });
    items.push_back({ "退出", "Quit", true, [] { return "quit"; } });
    return items;
}

void print_menu(Language language, const std::vector<MenuItem>& items, bool logged_in)
{
    std::cout << "\n==== " << text(language, "可用命令", "Available Commands") << " ====";
    std::cout << "  " << text(language, logged_in ? "已登录" : "未登录", logged_in ? "logged in" : "not logged in")
              << "\n";
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

}  // namespace

int main()
{
    try {
        setup_console();
        const Language language = choose_language();
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

        if (connect(socket_handle, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            std::cerr << text(language, "连接失败，请先启动 mmdemo_server。\n",
                              "connect failed. Start mmdemo_server first.\n");
            closesocket(socket_handle);
            return 1;
        }

        std::cout << text(language, "已连接到 127.0.0.1:7878。服务端响应仍以原始战报文本显示。\n",
                          "Connected to 127.0.0.1:7878. Server responses are shown as raw battle text.\n");

        std::atomic<bool> running{ true };
        std::thread receiver(receive_loop, socket_handle, std::ref(running));

        bool logged_in = false;
        while (running.load()) {
            const auto items = make_menu(language, logged_in);
            print_menu(language, items, logged_in);
            const int choice = read_choice();
            if (choice < 1 || static_cast<size_t>(choice) > items.size()) {
                std::cout << text(language, "无效选择。\n", "Invalid choice.\n");
                continue;
            }

            std::string line = items[static_cast<size_t>(choice - 1)].build_command();
            line = trim_copy(line);
            if (line.empty()) {
                std::cout << text(language, "命令已取消。\n", "Command canceled.\n");
                continue;
            }

            if (!send_all(socket_handle, line + "\n")) {
                break;
            }

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