local util = require("util")

local Protocol = {}

local aliases = {
    skill = "heavy",
    heavy_strike = "heavy",
}

function Protocol.parse(line)
    local words = util.split_words(line)
    local cmd = words[1]
    if cmd == nil then
        return nil
    end

    if cmd == "attack" or cmd == "skill" or cmd == "heavy" or cmd == "defend" or cmd == "heal" or cmd == "fire" then
        return {
            cmd = "action",
            player = words[2],
            skill = aliases[cmd] or cmd,
            target = words[3],
        }
    end

    return {
        cmd = cmd,
        arg1 = words[2],
        arg2 = words[3],
        arg3 = words[4],
    }
end

return Protocol
