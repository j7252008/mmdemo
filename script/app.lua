local Battle = require("battle")
local Player = require("player")
local Protocol = require("protocol")
local config = require("config")

local App = {
    players = {},
    waiting_queue = {},
    battles = {},
    battle_history = {},
    next_battle_id = 1,
}

local HELP = [[
commands:
  login <name>             create a text client/player
  pve <name> [monster]     start a PVE battle, monsters: slime / fox / bandit
  queue <name>             enter 1v1 matchmaking
  attack <name> [target]   normal attack
  heavy <name> [target]    heavy strike, costs 8 MP
  skill <name> [target]    alias of heavy
  fire <name> [target]     fire charm, costs 10 MP
  defend <name>            reduce incoming damage this round
  heal <name> [target]     heal self or ally, costs 6 MP
  players                  list player profiles
  monsters                 list monster templates
  skills                   list skill templates
  state                    print server state
  log [battle_id]          print latest or selected battle log
  help                     show commands
  quit                     exit
]]

function App.run()
    math.randomseed(os.time())
    print("mmdemo LuaJIT text MMO prototype")
    print(HELP)

    while true do
        io.write("> ")
        local line = io.read("*line")
        if line == nil then
            break
        end

        local ok, err = pcall(function()
            App.handle_line(line)
        end)
        if not ok then
            print("[error] " .. tostring(err))
        end
    end
end

function App.handle_line(line)
    local message = Protocol.parse(line)
    if message == nil or message.cmd == nil or message.cmd == "" then
        return
    end

    local cmd = message.cmd
    if cmd == "help" then
        print(HELP)
    elseif cmd == "quit" then
        os.exit(0)
    elseif cmd == "login" then
        App.login(message.arg1)
    elseif cmd == "queue" then
        App.queue(message.arg1)
    elseif cmd == "pve" then
        App.start_pve(message.arg1, message.arg2 or "slime")
    elseif cmd == "action" then
        App.submit_action(message.player, message.skill, message.target)
    elseif cmd == "players" then
        App.print_players()
    elseif cmd == "monsters" then
        App.print_monsters()
    elseif cmd == "skills" then
        App.print_skills()
    elseif cmd == "state" then
        App.print_state()
    elseif cmd == "log" then
        App.print_log(message.arg1)
    else
        print("[error] unknown command, type help")
    end
end

function App.login(name)
    if name == nil or name == "" then
        print("[error] name is required")
        return
    end
    if App.players[name] ~= nil then
        print("[error] " .. name .. " is already online")
        return
    end

    App.players[name] = Player.new(name)
    print("[server] " .. name .. " entered the world")
end

function App.start_pve(name, monster_id)
    local player = App.require_player(name)
    if player == nil then
        return
    end
    if player.battle ~= nil then
        print("[error] " .. name .. " is already in battle")
        return
    end
    if config.monsters[monster_id] == nil then
        print("[error] unknown monster: " .. tostring(monster_id))
        return
    end

    player.queued = false
    App.remove_from_queue(player)

    local battle = App.create_battle({player}, {}, {
        mode = "pve",
        monsters = {monster_id},
    })
    battle:start()
end

function App.queue(name)
    local player = App.require_player(name)
    if player == nil then
        return
    end
    if player.battle ~= nil then
        print("[error] " .. name .. " is already in battle")
        return
    end
    if player.queued then
        print("[server] " .. name .. " is already queued")
        return
    end

    local opponent = App.pop_waiting_opponent(player)
    if opponent == nil then
        player.queued = true
        App.waiting_queue[#App.waiting_queue + 1] = player
        print("[server] " .. name .. " queued")
        return
    end

    opponent.queued = false
    player.queued = false

    local battle = App.create_battle({opponent}, {player}, {mode = "pvp"})
    battle:start()
end

function App.create_battle(left_players, right_players, options)
    local battle_id = "B" .. tostring(App.next_battle_id)
    App.next_battle_id = App.next_battle_id + 1

    options = options or {}
    options.on_finish = function(battle)
        App.battles[battle.id] = nil
        App.battle_history[battle.id] = battle
    end

    local battle = Battle.new(battle_id, left_players, right_players, options)
    App.battles[battle_id] = battle
    return battle
end

function App.pop_waiting_opponent(player)
    while #App.waiting_queue > 0 do
        local opponent = table.remove(App.waiting_queue, 1)
        opponent.queued = false
        if opponent ~= player and opponent.battle == nil then
            return opponent
        end
    end
    return nil
end

function App.remove_from_queue(player)
    for i = #App.waiting_queue, 1, -1 do
        if App.waiting_queue[i] == player then
            table.remove(App.waiting_queue, i)
        end
    end
end

function App.submit_action(name, skill_id, target_id)
    local player = App.require_player(name)
    if player == nil then
        return
    end
    if player.battle == nil then
        print("[error] " .. name .. " is not in battle")
        return
    end

    local ok, err = player.battle:submit_action(player, skill_id, target_id)
    if not ok then
        print("[error] " .. err)
    end
end

function App.require_player(name)
    if name == nil or name == "" then
        print("[error] player name is required")
        return nil
    end

    local player = App.players[name]
    if player == nil then
        print("[error] unknown player: " .. name)
        return nil
    end

    return player
end

function App.print_players()
    local names = {}
    for name, _ in pairs(App.players) do
        names[#names + 1] = name
    end
    table.sort(names)

    if #names == 0 then
        print("[server] no players")
        return
    end

    for i = 1, #names do
        local player = App.players[names[i]]
        print("[player] " .. player.name .. " level=" .. tostring(player.level) .. " exp=" .. tostring(player.exp) .. " gold=" .. tostring(player.gold))
    end
end

function App.print_monsters()
    local ids = {}
    for id, _ in pairs(config.monsters) do
        ids[#ids + 1] = id
    end
    table.sort(ids)
    for i = 1, #ids do
        local monster = config.monsters[ids[i]]
        print("[monster] " .. ids[i] .. " name=" .. monster.name .. " level=" .. monster.level .. " hp=" .. monster.max_hp .. " reward=" .. monster.exp .. "exp/" .. monster.gold .. "gold")
    end
end

function App.print_skills()
    local ids = {}
    for id, _ in pairs(config.skills) do
        ids[#ids + 1] = id
    end
    table.sort(ids)
    for i = 1, #ids do
        local skill = config.skills[ids[i]]
        print("[skill] " .. ids[i] .. " name=" .. skill.name .. " kind=" .. skill.kind .. " mp=" .. tostring(skill.mp_cost))
    end
end

function App.print_state()
    App.print_players()

    local queued = {}
    for i = 1, #App.waiting_queue do
        queued[#queued + 1] = App.waiting_queue[i].name
    end
    print("[server] queue: " .. table.concat(queued, ", "))

    local active = {}
    for id, battle in pairs(App.battles) do
        active[#active + 1] = id
        battle:print_state()
    end
    table.sort(active)
    print("[server] active battles: " .. table.concat(active, ", "))
end

function App.print_log(battle_id)
    if battle_id == nil then
        battle_id = "B" .. tostring(App.next_battle_id - 1)
    end

    local battle = App.battles[battle_id] or App.battle_history[battle_id]
    if battle == nil then
        print("[error] unknown battle: " .. tostring(battle_id))
        return
    end
    battle:print_log()
end

return App
