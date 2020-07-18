local class_builder = require('utils.class')

local loader = class_builder.register('utils.loader', class_builder.singleton)
loader.loaded_mods = {}

-- 移除模块
function loader.remove(modname)
    local ret = package.loaded[modname]
    package.loaded[modname] = nil
    return ret
end

-- 安全加载模块, 不允许加载userdata
function loader.load(modname)
    local ret = require(modname)
    if 'userdata' == type(ret) then
        package.loaded[modname] = nil
        ret = nil
        log_error('try to load userdata using utils.loader.load(%s)', tostring(modname))
    else
--        if not loader.loaded_mods[modname] then
--            log_debug('load new module %s', modname)
--        end

        loader.loaded_mods[modname] = true
    end

    return ret
end

-- 安全加载table, 不允许加载userdata
function loader.load_table(table_obj, prefix, root)
    assert('table' == type(table_obj))
    prefix = prefix or ''
    root = root or _G
    for k, v in pairs(table_obj) do
        -- 子结构
        if 'table' == type(v) then
            if nil == root[k] then
                root[k] = {}
            end
            loader.load_table(v, prefix .. k .. '.', root[k])

        -- 列表文件
        elseif 'string' == type(k) and 'string' == type(v) then
            loader.load_list(v, prefix .. k .. '.', root)

        -- 普通文件: table list
        elseif 'number' == type(k) and 'string' == type(v) then
            loader.load(prefix .. v)
            
        -- 普通文件: key-value配置
        else
            root[k] = loader.load(prefix .. k)
        end
    end
end

-- 安全加载列表, 不允许加载userdata
function loader.load_list(modname, prefix, root)
    prefix = prefix or ''
    root = root or _G

    local ret = loader.load(prefix .. modname)
    if 'userdata' == type(ret) then
        log_error('load list file "' .. prefix .. modname .. '" failed')
        return false
    end

    loader.load_table(ret, prefix, root)
    return true
end

-- 清空所有非预置模块
function loader.clear()
    for k, v in pairs(loader.loaded_mods) do
        package.loaded[k] = nil
        --log_debug('remove package %s', k)
    end
    package.loaded['utils.adaptor'] = nil
    package.loaded['utils.vardump'] = nil
    package.loaded['utils.conf'] = nil
    package.loaded['utils.class'] = nil
    package.loaded['utils.loader'] = nil
    package.loaded['utils.event'] = nil
    
    -- vardump(package.loaded, {ostream = log_stream, recursive = 1})

    -- 清空后要保证loader首先被加载，并初始化utils包
    class_builder = require('utils.class')
    loader = require('utils.loader')
    require('utils.event')
end

--先清理再重加载传入的多个lua文件
function loader.reload(...)
    loader.clear()
    local args = {...}
    for k, v in ipairs(args) do
        loader.load_list(v)
    end
end

--先清理再重加载
function loader.smart_reload()
    local loaded_mods_temp = loader.loaded_mods
	
    loader.clear()
  
    for k, v in pairs(loaded_mods_temp) do
        loader.load(k)
    end
end

-- 安全预加载模块, 不允许加载userdata
-- 不会被clear清空
function loader.preload(modname)
    local ret = loader.load(modname)
    if 'userdata' ~= type(ret) then
        package.preload[modname] = ret
    end

    return ret
end

-- [DEBUG] 打印加载的模块
function loader.print_loaded_module()
    for k,v in pairs(package.loaded) do
        print(k..'    '..tostring(v))
    end 
end

return loader
