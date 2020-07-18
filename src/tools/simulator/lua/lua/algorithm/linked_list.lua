local clazz = {
    __classname = 'linked_list',
    __type = 'class'
}

local iter = {
    __classname = 'linked_list.iterator',
    __type = 'class'
}

local function iter_new_fn(ele)
    local ret = {
        __type = 'object',
        __tostring = iter.__tostring,
        data = ele or nil,
        next_node = nil,
        prev_node = nil,
        list_obj = nil
    }
    ret.next_node = ret
    ret.prev_node = ret
    setmetatable(ret, ret)
    rawset(ret, '__index', iter)
    return ret
end

function iter:get()
    return self.data
end

function iter:set(d)
    local ret = self.data
    self.data = d
    return ret
end

function iter:next()
    return self.next_node
end

function iter:prev()
    return self.prev_node
end

function iter:insert(it)
    assert(it.__type == self.__type and it.__classname == iter.__classname)
    if self == it then
        return self
    end

    -- old linked_list combine iterator
    it:remove()

    -- new linked_list insert iterator
    self.prev_node.next_node = it
    it.prev_node = self.prev_node
    it.next_node = self
    self.prev_node = it

    if self.list_obj then
        self.list_obj.size_cache = self.list_obj.size_cache + 1
        it.list_obj = self.list_obj
    end

    return it
end

function iter:remove()
    if self.list_obj then
        self.list_obj.size_cache = self.list_obj.size_cache - 1
        if self.list_obj.head == self then
            self.list_obj.head = self:next()
        end
    end
    self.list_obj = nil

    self.prev_node.next_node = self.next_node
    self.next_node.prev_node = self.prev_node

    self.next_node = self
    self.prev_node = self
end

function iter:__tostring()
    return string.format('[%s : %s] %s', self.__type, self.__classname, tostring(self:get()))
end

function clazz.new()
    local end_iter = iter_new_fn(nil)
    end_iter.remove = function()
        error('end iterator can not be removed')
    end

    local ret = {
        __type = 'object',
        __tostring = clazz.__tostring,
        head = end_iter,
        tail = end_iter,
        size_cache = 0
    }
    setmetatable(ret, ret)
    rawset(ret, '__index', clazz)

    end_iter.list_obj = ret
    return ret
end

function clazz:empty()
    return self.head == self.tail
end

function clazz:size()
    return self.size_cache
end

function clazz.__len(self)
    return self:size()
end

function clazz:push_back(ele)
    local res = self:lend():insert(iter_new_fn(ele))
    if self.head == self.tail then
        self.head = res
    end
end

function clazz:push_front(ele)
    self.head = self:lbegin():insert(iter_new_fn(ele))
end

function clazz:pop_front()
    if self:empty() then
        return nil
    end

    local ret = self.head
    self.head = ret:next()
    ret:remove()
    return ret:get()
end

function clazz:pop_back()
    if self:empty() then
        return nil
    end

    local ret = self.tail:prev()
    if ret == self.head then
        self.head = self.tail
    end

    ret:remove()
    return ret:get()
end

function clazz:front()
    return self:lbegin():get()
end

function clazz:back()
    return self:rbegin():get()
end

function clazz:lbegin()
    return self.head
end

function clazz:lend()
    return self.tail
end

function clazz:rbegin()
    return self.tail:prev()
end

function clazz:rend()
    return self.head:prev()
end

function clazz:insert(it, ele)
    return it:insert(iter_new_fn(ele))
end

function clazz:to_array()
    local ret = {}
    local it = self:lbegin()
    while it ~= self:lend() do
        table.insert(ret, it:get())
        it = it:next()
    end
    return ret
end

function clazz:__tostring()
    return string.format('[%s : %s] %s', self.__type, self.__classname, table.concat(self:to_array(), ', '))
end

return clazz
