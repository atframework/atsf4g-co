--region union_set.lua
--Author : 文韬
--Date   : 2014/10/22
--此文件由[BabeLua]插件自动生成

-- 并查集算法 - begin
function union_find(union_set, id)
    union_set[id] = union_set[id] or id
    if union_set[id] == id then
        return id
    end

    union_set[id] = union_find(union_set, union_set[id])
    return union_set[id]
end

function union_merge(union_set, left, right)
    union_set[left] = union_set[left] or left
    union_set[right] = union_set[right] or right
    
    local root_left = union_find(union_set, left)
    local root_right = union_find(union_set, right)
    if root_left == root_right then
        return
    end
    
    if root_left < root_right then
        union_set[root_right] = root_left
    else
        union_set[root_left] = root_right
    end
end
-- 并查集算法 - end


--endregion
