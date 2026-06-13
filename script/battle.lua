local config = require("config")
local util = require("util")

local Battle = {}
Battle.__index = Battle

local function is_alive(fighter)
    return fighter.hp > 0
end

local function make_player_fighter(player, side)
    local base = config.player_base
    return {
        id = player.name,
        name = player.name,
        type = "player",
        side = side,
        player = player,
        level = player.level,
        hp = base.max_hp + (player.level - 1) * 12,
        max_hp = base.max_hp + (player.level - 1) * 12,
        mp = base.max_mp + (player.level - 1) * 3,
        max_mp = base.max_mp + (player.level - 1) * 3,
        attack = base.attack + (player.level - 1) * 2,
        defense = base.defense + (player.level - 1),
        speed = base.speed,
        skills = {"attack", "heavy", "defend", "heal", "fire"},
        defending = false,
    }
end

local function make_monster_fighter(monster_id, index, side)
    local monster = config.monsters[monster_id]
    if monster == nil then
        return nil
    end

    return {
        id = monster_id .. tostring(index),
        name = monster.name .. "#" .. tostring(index),
        type = "monster",
        side = side,
        monster_id = monster_id,
        level = monster.level,
        hp = monster.max_hp,
        max_hp = monster.max_hp,
        mp = monster.max_mp,
        max_mp = monster.max_mp,
        attack = monster.attack,
        defense = monster.defense,
        speed = monster.speed,
        skills = monster.skills,
        exp = monster.exp,
        gold = monster.gold,
        defending = false,
    }
end

function Battle.new(id, left_players, right_players, options)
    options = options or {}

    local self = {
        id = id,
        mode = options.mode or "pvp",
        round = 0,
        fighters = {},
        pending_actions = {},
        events = {},
        closed = false,
        on_finish = options.on_finish,
    }

    for i = 1, #left_players do
        local fighter = make_player_fighter(left_players[i], "left")
        self.fighters[#self.fighters + 1] = fighter
        left_players[i].battle = self
    end

    if options.monsters ~= nil then
        for i = 1, #options.monsters do
            self.fighters[#self.fighters + 1] = make_monster_fighter(options.monsters[i], i, "right")
        end
    else
        for i = 1, #right_players do
            local fighter = make_player_fighter(right_players[i], "right")
            self.fighters[#self.fighters + 1] = fighter
            right_players[i].battle = self
        end
    end

    return setmetatable(self, Battle)
end

function Battle:add_event(kind, text)
    local event = {
        round = self.round,
        kind = kind,
        text = text,
    }
    self.events[#self.events + 1] = event
    print("[battle] " .. text)
end

function Battle:start()
    self:add_event("start", "Battle " .. self.id .. " starts: " .. self:side_names("left") .. " vs " .. self:side_names("right"))
    self:begin_round()
end

function Battle:begin_round()
    self.round = self.round + 1
    self.pending_actions = {}
    for i = 1, #self.fighters do
        self.fighters[i].defending = false
    end
    self:add_event("round_start", "Round " .. self.round .. " begins.")
    self:submit_ai_actions()
    self:print_state()
    if self:all_ready() then
        self:resolve_ready_round()
    end
end

function Battle:submit_action(player, skill_id, target_id)
    if self.closed then
        return false, "battle is closed"
    end

    local fighter = self:fighter_for_player(player)
    if fighter == nil or not is_alive(fighter) then
        return false, "player cannot act"
    end

    local skill = config.skills[skill_id]
    if skill == nil then
        return false, "unknown skill"
    end
    if not util.contains(fighter.skills, skill_id) then
        return false, "fighter cannot use skill"
    end

    local target = self:resolve_target(fighter, skill, target_id)
    if target == nil then
        return false, "no valid target"
    end

    self.pending_actions[fighter.id] = {
        actor_id = fighter.id,
        skill_id = skill_id,
        target_id = target.id,
    }

    print("[server] action locked: " .. player.name .. " -> " .. skill.name .. " / " .. target.name)

    if self:all_ready() then
        self:resolve_ready_round()
    end

    return true
end

function Battle:submit_ai_actions()
    for i = 1, #self.fighters do
        local fighter = self.fighters[i]
        if fighter.type == "monster" and is_alive(fighter) then
            local skill_id = self:choose_ai_skill(fighter)
            local skill = config.skills[skill_id]
            local target = self:resolve_target(fighter, skill, nil)
            self.pending_actions[fighter.id] = {
                actor_id = fighter.id,
                skill_id = skill_id,
                target_id = target.id,
            }
        end
    end
end

function Battle:choose_ai_skill(fighter)
    if fighter.hp * 100 <= fighter.max_hp * 35 and util.contains(fighter.skills, "heal") and fighter.mp >= config.skills.heal.mp_cost then
        return "heal"
    end

    local available = {}
    for i = 1, #fighter.skills do
        local skill_id = fighter.skills[i]
        local skill = config.skills[skill_id]
        if skill ~= nil and fighter.mp >= skill.mp_cost and skill.kind ~= "defend" then
            available[#available + 1] = skill_id
        end
    end

    if #available == 0 then
        return "attack"
    end
    return available[math.random(1, #available)]
end

function Battle:resolve_ready_round()
    self:resolve_round()
    self:print_state()

    if self:winner_side() == nil then
        self:begin_round()
    else
        self:finish()
    end
end

function Battle:resolve_round()
    local order = {}
    for i = 1, #self.fighters do
        if is_alive(self.fighters[i]) then
            order[#order + 1] = self.fighters[i]
        end
    end

    table.sort(order, function(a, b)
        if a.speed == b.speed then
            return a.id < b.id
        end
        return a.speed > b.speed
    end)

    for i = 1, #order do
        local actor = order[i]
        if is_alive(actor) then
            local action = self.pending_actions[actor.id]
            if action ~= nil then
                local target = self:fighter_by_id(action.target_id)
                local skill = config.skills[action.skill_id]
                if target == nil or not is_alive(target) then
                    target = self:resolve_target(actor, skill, nil)
                end
                if target ~= nil then
                    self:resolve_action(actor, target, skill)
                end
                if self:winner_side() ~= nil then
                    break
                end
            end
        end
    end
end

function Battle:resolve_action(actor, target, skill)
    if actor.mp < skill.mp_cost then
        self:add_event("fail", actor.name .. " tried " .. skill.name .. " but lacked MP, using Attack.")
        skill = config.skills.attack
        target = self:resolve_target(actor, skill, nil)
    end

    if skill.kind == "defend" then
        actor.defending = true
        self:add_event("defend", actor.name .. " braces for impact.")
        return
    end

    actor.mp = actor.mp - skill.mp_cost

    if skill.kind == "heal" then
        local amount = math.random(skill.min_heal, skill.max_heal) + actor.level * 2
        local before = target.hp
        target.hp = util.clamp(target.hp + amount, 0, target.max_hp)
        self:add_event("heal", actor.name .. " uses " .. skill.name .. " on " .. target.name .. ": +" .. tostring(target.hp - before) .. " HP.")
        return
    end

    local base = actor.attack + (skill.power or 0) + math.random(0, 8)
    local damage = math.max(1, base - target.defense)
    if target.defending then
        damage = math.max(1, math.floor(damage / 2))
    end

    target.hp = util.clamp(target.hp - damage, 0, target.max_hp)
    self:add_event("damage", actor.name .. " uses " .. skill.name .. " on " .. target.name .. ": " .. tostring(damage) .. " damage.")
    if not is_alive(target) then
        self:add_event("death", target.name .. " falls.")
    end
end

function Battle:all_ready()
    for i = 1, #self.fighters do
        local fighter = self.fighters[i]
        if is_alive(fighter) and fighter.type == "player" and self.pending_actions[fighter.id] == nil then
            return false
        end
    end
    return true
end

function Battle:resolve_target(actor, skill, target_id)
    if skill.target == "self" then
        return actor
    end

    if target_id ~= nil then
        local target = self:fighter_by_id(target_id)
        if target ~= nil and is_alive(target) then
            if skill.target == "enemy" and target.side ~= actor.side then
                return target
            end
            if skill.target == "ally" and target.side == actor.side then
                return target
            end
        end
    end

    local candidates = {}
    for i = 1, #self.fighters do
        local fighter = self.fighters[i]
        if is_alive(fighter) then
            if skill.target == "enemy" and fighter.side ~= actor.side then
                candidates[#candidates + 1] = fighter
            elseif skill.target == "ally" and fighter.side == actor.side then
                candidates[#candidates + 1] = fighter
            end
        end
    end

    if #candidates == 0 then
        return nil
    end

    table.sort(candidates, function(a, b)
        if a.hp == b.hp then
            return a.id < b.id
        end
        return a.hp < b.hp
    end)

    return candidates[1]
end

function Battle:fighter_for_player(player)
    for i = 1, #self.fighters do
        if self.fighters[i].player == player then
            return self.fighters[i]
        end
    end
    return nil
end

function Battle:fighter_by_id(id)
    for i = 1, #self.fighters do
        if self.fighters[i].id == id or self.fighters[i].name == id then
            return self.fighters[i]
        end
    end
    return nil
end

function Battle:side_names(side)
    local names = {}
    for i = 1, #self.fighters do
        if self.fighters[i].side == side then
            names[#names + 1] = self.fighters[i].name
        end
    end
    return table.concat(names, ", ")
end

function Battle:winner_side()
    local left_alive = false
    local right_alive = false

    for i = 1, #self.fighters do
        if is_alive(self.fighters[i]) then
            if self.fighters[i].side == "left" then
                left_alive = true
            else
                right_alive = true
            end
        end
    end

    if left_alive and not right_alive then
        return "left"
    end
    if right_alive and not left_alive then
        return "right"
    end
    return nil
end

function Battle:finish()
    local winner = self:winner_side()
    self:add_event("end", self:side_names(winner) .. " wins.")
    self:grant_rewards(winner)
    self:close()
    if self.on_finish ~= nil then
        self.on_finish(self)
    end
end

function Battle:grant_rewards(winner)
    if winner ~= "left" or self.mode ~= "pve" then
        return
    end

    local exp = 0
    local gold = 0
    for i = 1, #self.fighters do
        local fighter = self.fighters[i]
        if fighter.type == "monster" then
            exp = exp + fighter.exp
            gold = gold + fighter.gold
        end
    end

    for i = 1, #self.fighters do
        local fighter = self.fighters[i]
        if fighter.side == "left" and fighter.type == "player" then
            local leveled = fighter.player:add_reward(exp, gold, config.exp_to_next)
            local suffix = ""
            if leveled then
                suffix = " Level up to " .. tostring(fighter.player.level) .. "!"
            end
            self:add_event("reward", fighter.player.name .. " receives " .. tostring(exp) .. " exp and " .. tostring(gold) .. " gold." .. suffix)
        end
    end
end

function Battle:close()
    self.closed = true
    for i = 1, #self.fighters do
        if self.fighters[i].player ~= nil then
            self.fighters[i].player.battle = nil
        end
    end
end

function Battle:print_state()
    local parts = {}
    for i = 1, #self.fighters do
        local fighter = self.fighters[i]
        parts[#parts + 1] = fighter.id .. "=" .. fighter.name .. " HP " .. fighter.hp .. "/" .. fighter.max_hp .. " MP " .. fighter.mp .. "/" .. fighter.max_mp
    end
    print("[state] " .. table.concat(parts, " | "))
end

function Battle:print_log()
    for i = 1, #self.events do
        local event = self.events[i]
        print("[log][" .. tostring(event.round) .. "][" .. event.kind .. "] " .. event.text)
    end
end

return Battle
