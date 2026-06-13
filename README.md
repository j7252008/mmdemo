# mmdemo

文字版回合制 MMO 服务端原型。当前重点不是客户端动画素材，而是先把服务端核心规则、TCP 文本协议、配置、存档和测试链路跑通。

核心实现使用 C++17；`script/*.lua` 作为 LuaJIT / Lua 5.1 兼容的早期脚本草稿保留。配置和玩家存档都使用 JSON，解析库为 `3rdparty/nlohmann/json.hpp`。

## 当前功能

- 玩家创建、登录、离线、重连
- PVE 遭遇战，支持单怪和多怪遭遇
- PVP 1v1 排队匹配
- 回合制战斗房间：收集行动、怪物 AI 自动行动、按速度结算
- 技能：普通攻击、重击、火符、防御、治疗
- 战斗道具、背包、掉落、商店购买
- 任务：接取、击杀进度、领奖
- 战斗状态查看、战斗日志回放
- TCP 服务端和文本客户端
- JSON 配置加载和 JSON 玩家存档
- 覆盖核心功能的自动测试

## 构建和运行

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

运行本地命令行版：

```powershell
.\build\Debug\mmdemo_cpp.exe
```

启动 TCP 服务端：

```powershell
.\build\Debug\mmdemo_server.exe
```

启动文本客户端：

```powershell
.\build\Debug\mmdemo_client.exe
```

客户端启动后会先选择中文或英文，然后显示当前可用命令菜单。选择数字即可发送命令；需要额外参数时，客户端会继续提示输入遭遇 id、目标、道具 id 或任务 id。

运行自动测试：

```powershell
.\build\Debug\mmdemo_tests.exe
```

Lua 草稿也可以单独运行：

```powershell
luajit .\script\main.lua
```

## 目录

```text
mmdemo/
  CMakeLists.txt
  README.md
  3rdparty/
    nlohmann/
      json.hpp
      json_fwd.hpp
  data/
    config.json
    players.json        # 执行 save 后生成
  src/
    battle/
      Battle.cpp
      Battle.h
    core/
      ConfigJson.cpp
      Output.h
      Types.h
    game/
      GameServer.cpp
      GameServer.h
    net/
      SessionCommand.cpp
      SessionCommand.h
    game_core.h
    main.cpp
    net_client.cpp
    net_server.cpp
    tests.cpp
  script/
    main.lua
    app.lua
    battle.lua
    config.lua
    player.lua
    protocol.lua
    util.lua
```

## JSON 配置

默认配置文件是 `data/config.json`。`GameServer()` 启动时会尝试读取它；如果文件不存在或 JSON 无效，会回退到 C++ 内置默认配置。

配置支持这些顶层表：

```json
{
  "skills": [],
  "items": [],
  "monsters": [],
  "quests": [],
  "encounters": []
}
```

只写其中一部分也可以。某个表不存在时会保留内置默认表；某个表存在时会替换对应的默认表。

一个遭遇配置示例：

```json
{
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
}
```

## 玩家存档

默认存档文件是 `data/players.json`。执行 `save` 时会写入；执行 `load` 时会读取。

存档包含玩家的持久数据：

```json
{
  "version": 1,
  "players": [
    {
      "name": "alice",
      "level": 1,
      "exp": 12,
      "gold": 8,
      "inventory": {
        "potion": 4
      },
      "quests": {
        "slime_hunter": {
          "progress": 1,
          "claimed": false
        }
      }
    }
  ]
}
```

加载存档时只恢复持久数据。在线状态、排队状态、当前战斗房间不会恢复，玩家加载后默认是 offline。

## 本地命令示例

```text
login alice
quest accept alice slime_hunter
pve alice forest
heavy alice fox2
heavy alice fox2
heavy alice fox2
use alice hi_potion
attack alice
attack alice
quest claim alice slime_hunter
shop
buy alice potion 1
players
save
load
players
quit
```

本地命令版使用显式玩家名，方便在一个进程里调试多个玩家。

常用命令：

```text
login <name>                  创建或重连玩家
logout <name>                 玩家不在战斗中时标记离线
forfeit <name>                认输并结束当前战斗
pve <name> [encounter]        开始 PVE，默认 slime
queue <name>                  加入 PVP 匹配队列
attack <name> [target]        普通攻击
heavy <name> [target]         重击，消耗 8 MP
skill <name> [target]         heavy 的别名
fire <name> [target]          火符，消耗 10 MP
defend <name>                 防御
heal <name> [target]          治疗自己或友方
use <name> <item> [target]    战斗中使用道具
inventory <name>              查看背包
bag <name>                    inventory 的别名
give <name> <item> [n]        调试发放道具
shop                          查看商店
buy <name> <item> [n]         购买道具
quests                        查看任务模板
quest accept <name> <id>      接任务
quest list <name>             查看玩家任务
quest claim <name> <id>       领取任务奖励
players                       查看玩家状态
monsters                      查看怪物和遭遇配置
items                         查看道具配置
skills                        查看技能配置
state                         查看服务端状态
log [battle_id]               查看战斗日志
save                          保存玩家到 data/players.json
load                          从 data/players.json 读取玩家
help                          显示帮助
quit                          退出
```

## TCP 客户端

`mmdemo_server.exe` 监听 `127.0.0.1:7878`。`mmdemo_client.exe` 是菜单式文本客户端：

1. 启动后选择 `中文` 或 `English`。
2. 未登录时菜单只显示登录、查询、读取存档、退出等安全命令。
3. 登录成功后菜单会显示 PVE、PVP、技能、道具、任务、商店、状态、日志、保存/读取等命令。
4. 选择数字即可发送命令；需要参数时按提示输入。

客户端菜单会把数字选择转换成服务端文本协议。例如：

```text
pve slime
queue
heavy bob
fire bob
use potion
inventory
buy potion 1
quest accept slime_hunter
quest list
quest claim slime_hunter
forfeit
quit
```

同一个连接不能二次登录，也不能冒充别的玩家发行动。客户端断开时，服务端会尝试清理当前绑定玩家：普通状态会 logout；战斗中会先 forfeit 再 logout，避免战斗房间卡住。

当前客户端 UI 支持中文/英文；服务端返回的战报和错误仍以服务端原始文本显示。

## 测试覆盖

`mmdemo_tests.exe` 是轻量无框架测试程序，直接调用 `GameServer::execute()`。当前覆盖：

- 命令目录和只读查询
- TCP session 命令翻译和登录前白名单
- PVE 胜利、奖励、掉落、背包
- PVP 匹配和行动提交
- 多怪遭遇和聚合奖励
- 技能、防御、战斗状态、战斗日志
- 道具使用、商店购买
- 任务接取、进度、领奖
- JSON 配置加载
- JSON 存档 save/load
- logout、forfeit、重连
- 常见错误路径

每次修改战斗、任务、商店、存档、配置或 TCP session 规则后，建议运行：

```powershell
cmake --build build --config Debug
.\build\Debug\mmdemo_tests.exe
```