local M = {}

M.skills = {
    attack = {
        id = "attack",
        name = "Attack",
        mp_cost = 0,
        target = "enemy",
        power = 0,
        kind = "damage",
    },
    heavy = {
        id = "heavy",
        name = "Heavy Strike",
        mp_cost = 8,
        target = "enemy",
        power = 14,
        kind = "damage",
    },
    defend = {
        id = "defend",
        name = "Defend",
        mp_cost = 0,
        target = "self",
        kind = "defend",
    },
    heal = {
        id = "heal",
        name = "Heal",
        mp_cost = 6,
        target = "ally",
        kind = "heal",
        min_heal = 18,
        max_heal = 28,
    },
    fire = {
        id = "fire",
        name = "Fire Charm",
        mp_cost = 10,
        target = "enemy",
        power = 20,
        kind = "damage",
    },
}

M.player_base = {
    max_hp = 120,
    max_mp = 30,
    attack = 18,
    defense = 6,
    speed = 12,
}

M.monsters = {
    slime = {
        name = "Green Slime",
        level = 1,
        max_hp = 75,
        max_mp = 10,
        attack = 13,
        defense = 3,
        speed = 7,
        skills = {"attack"},
        exp = 12,
        gold = 8,
    },
    fox = {
        name = "Mountain Fox",
        level = 2,
        max_hp = 92,
        max_mp = 18,
        attack = 16,
        defense = 4,
        speed = 13,
        skills = {"attack", "heavy"},
        exp = 18,
        gold = 12,
    },
    bandit = {
        name = "Wild Bandit",
        level = 3,
        max_hp = 135,
        max_mp = 26,
        attack = 20,
        defense = 7,
        speed = 10,
        skills = {"attack", "heavy", "heal"},
        exp = 30,
        gold = 22,
    },
}

function M.exp_to_next(level)
    return 40 + (level - 1) * 25
end

return M
