local M = {}

function M.clamp(value, low, high)
    if value < low then
        return low
    end
    if value > high then
        return high
    end
    return value
end

function M.split_words(line)
    local words = {}
    for word in string.gmatch(line, "%S+") do
        words[#words + 1] = word
    end
    return words
end

function M.contains(list, value)
    for i = 1, #list do
        if list[i] == value then
            return true
        end
    end
    return false
end

function M.shallow_copy(source)
    local target = {}
    for key, value in pairs(source) do
        target[key] = value
    end
    return target
end

function M.join_names(list)
    local names = {}
    for i = 1, #list do
        names[#names + 1] = list[i].name
    end
    return table.concat(names, ", ")
end

return M
