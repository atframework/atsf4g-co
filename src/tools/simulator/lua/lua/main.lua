do
    local info = '============================= Load Lua Start ============================='
    if _G.jit then
        info = info .. '\n========== Lua JIT Version:' .. tostring(_G.jit.version) .. ' ==========\n\n\n'
    end

    if _G.lua_log then
        lua_log(0, 1, info)
    else
        print(info)
    end
end

math.randomseed(os.time())

local loader = require('utils.loader')