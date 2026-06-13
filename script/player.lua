local Player = {}
Player.__index = Player

function Player.new(name)
    local self = {
        name = name,
        level = 1,
        exp = 0,
        gold = 0,
        battle = nil,
        queued = false,
        online = true,
    }
    return setmetatable(self, Player)
end

function Player:add_reward(exp, gold, exp_to_next)
    self.exp = self.exp + exp
    self.gold = self.gold + gold

    local leveled = false
    while self.exp >= exp_to_next(self.level) do
        self.exp = self.exp - exp_to_next(self.level)
        self.level = self.level + 1
        leveled = true
    end

    return leveled
end

return Player
