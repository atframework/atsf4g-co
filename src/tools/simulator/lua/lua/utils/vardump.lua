--region vardump.lua
--Author : 文韬
--Date   : 2014/10/22
--此文件由[BabeLua]插件自动生成

_G.vardump_default = {
    show_all = false,               -- 显示隐藏对象（以__开头）
    show_metatable = false,         -- 展开metatable
    show_table_once = false,        -- 每个名称的table只显示一次
    ident = "    ",                 -- 缩进符号
    symbol_name = "__classname",    -- 符号名称保存位置
    recursive = nil,                -- 递归打印的层数（nil表示不限制打印层数 -_-||）  
    ostream = io.stdout             -- 输出目标
}

-- 额外拓展 -- vardump
_G.vardump = function (var, conf)
    local rm = {}

    local cfg = {};
    if table.clone then
        cfg = table.clone(_G.vardump_default)
    else
        for k, v in pairs(_G.vardump_default) do
            cfg[k] = _G.vardump_default[k]
        end
    end
    
    if "table" == type(conf) then
        for k, v in pairs(conf) do
            cfg[k] = v
        end
    end
    
    local function pvar(obj, ident, root, recursive)
        local t = type(obj)
        local ident_symbol = cfg.ident
        
        if nil ~= recursive then
            recursive = recursive - 1
        end
        
        if nil == t then
            cfg.ostream:write("nil")
        elseif "table" == t and (nil == recursive or recursive >= 0) then
            local name = tostring(obj)
            cfg.ostream:write(name)
            if nil == rawget(rm, name) then
                -- 防止环形引用
                rawset(rm, name, "   ### [reference to " .. root .. "]")
                
                cfg.ostream:write(" {\n")
                -- metatable
                if cfg.show_all or cfg.show_metatable then
                    local meta = getmetatable(obj)
                    cfg.ostream:write(ident .. ident_symbol .. "__metatable : ")
                    if cfg.show_metatable then
                        pvar(meta, ident .. ident_symbol, name, recursive)
                    else
                        cfg.ostream:write(tostring(meta))
                        if "table" == type(meta) and cfg.symbol_name and nil ~= meta[cfg.symbol_name] then
                            cfg.ostream:write(" [" .. tostring(meta[cfg.symbol_name]) .. "]")
                        end
                    end
                    cfg.ostream:write("\n")
                end
                
                for k, v in pairs(obj) do
                    if cfg.show_all or '__' ~= string.sub(k, 1, 2) then
                        cfg.ostream:write(ident .. ident_symbol)
                        pvar(k, ident .. ident_symbol, name, recursive)
                        cfg.ostream:write(" : ")
                        pvar(v, ident .. ident_symbol, name, recursive)
                        cfg.ostream:write("\n")
                    end
                end
                cfg.ostream:write(ident .. "}")
                
                
                -- 如果不是强制只显示一次table则允许再显示
                if cfg.show_table_once then
                    rawset(rm, name, nil)
                end
            else
                cfg.ostream:write(rawget(rm, name))
            end
            
        else
            cfg.ostream:write(tostring(obj))
        end
    end
    
    pvar(var, "", "{root}", cfg.recursive)
    cfg.ostream:write("\n")
    cfg.ostream:flush ()
end

--  额外拓展 -- vardbg_assert
_G.vardbg_assert = function (var, check, conf, callback)
    if check then
        return
    end
    
    vardump(var, conf)
    if nil ~= callback then
        callback()
    end
end

--  额外拓展 -- stackdump -- 用于c api的lua_pcall传入的hmsg,追加堆栈信息
_G.stackdump = function(msg)
    return debug.traceback(msg, 2)
end

return _G.vardump, _G.vardbg_assert, _G.stackdump

--endregion
