--[[
API 说明

注意:
- Promise实例的承若(状态改变)后,即为常量.
  常量的 :next(...), :catch(...) 为同步方法
  常量的 :resolve(...), :reject(...)为无效调用

创建实例:
  - p = Promise.new(name)
  - p = Promise.new(function(resolve, reject) ... end, name)
完成承若:
  - p:resolve(...)
  - p:reject(...)

创建常量
  - p = Promise.resolve(value, name)
  - p = Promise.reject(reason, name)

等待所有的'fulfilled', 或首个的'rejected'
  - Promise.all(...)
  - Promise.all{...}

等待首个的'fulfilled', 或首个的'rejected'
  - Promise.race(...)
  - Promise.race{...}
- 
  - Promise.update()
--------------------------------------------------------------------------------]]
local FULFILLED, REJECTED  = 'fulfilled', 'rejected'

local passthrough = function(x) return x end
local errorthrough = function(x) error(x, 0) end

local function callable_table(callback)
    local mt = getmetatable(callback)
    return type(mt) == 'table' and type(mt.__call) == 'function'
end
local function is_callable(value)
    local t = type(value)
    return t == 'function' or (t == 'table' and callable_table(value))
end
--------------------------------------------------------------------------------
local promise_queue={}
local promise_new
local promise_proto = { state='pending' }
local promise_mt = {__index = promise_proto }
local is_promise = function(obj)
    return getmetatable(obj) == promise_mt, 'TypeError: not a promise'
end

local transition = function(promise, state, value)
    assert(state == FULFILLED or state == REJECTED)
    assert(not rawget(promise, 'state'))
    if #promise > 0 then
        print('=', promise.name, state)
        table.insert(promise_queue, promise)
    end
    rawset(promise, 'state', state)
    rawset(promise, 'value', value)
    return promise
end
--------------------------------------------------------------------------------
local promise_reject = function(promise, reason)
    assert(is_promise(promise))
    if rawget(promise, 'state') then
        return promise
    else
        return transition(promise, REJECTED, reason)
    end
end
local promise_resolve = function(promise, x)
    assert(is_promise(promise))
    if rawget(promise, 'state') then
        return promise
    elseif type(x) ~= 'table' then
        return transition(promise, FULFILLED, x)
    elseif is_promise(x) then
        assert(promise ~= x, 'TypeError: cannot resolve a promise with itself')
        if not rawget(x, 'state') then
            return x:next(
            function(value) promise_resolve(promise, value) end,
            function(reason) promise_reject(promise, reason) end
            )
        else
            return promise_resolve(promise, rawget(x, 'value'))
        end
    elseif is_callable(x.next) then
        local success, result = pcall(next, x, 
        function(value) promise_resolve(promise, value) end,
        function(reason) promise_reject(promise, reason) end
        )
        if not success then
            return promise_reject(promise, result)
        elseif result then
            return promise_resolve(promise, result)
        else
            return promise
        end
    else
        return transition(promise, FULFILLED, x)
    end
end

local function promise_run (promise)
    if #promise == 0 then return end
    local value = rawget(promise, 'value')
    local fulfilled = rawget(promise, 'state')
    assert(fulfilled == FULFILLED or fulfilled == REJECTED)
    fulfilled = (fulfilled == FULFILLED)
    for f, obj in ipairs(promise) do
        promise[f] = nil
        if not rawget(obj[1], 'state') then
            f = fulfilled and obj[2] or obj[3]
            obj = obj[1]
            print('>', promise.name, obj.name)
            local success, result = pcall(f, value, obj)

            if not success then
                promise_reject(obj, result)
            elseif result then
                promise_resolve(obj, result)
            end
        end
    end
end
--------------------------------------------------------------------------------
function promise_proto:next(on_fulfilled, on_rejected, name)
    assert(is_promise(self))
    local state = rawget(self, 'state')
    if not name and type(on_rejected) == 'string' then name = on_rejected end
    if state then
        if state == FULFILLED and not is_callable(on_fulfilled) then
            return self  -- skip :catch
        elseif state == REJECTED and not is_callable(on_rejected) then
            return self -- skip :next
        end
    else
        on_fulfilled = is_callable(on_fulfilled) and on_fulfilled or passthrough
        on_rejected = is_callable(on_rejected) and on_rejected or errorthrough
    end
    if name then name = (rawget(self, 'name') or '') ..'/'..name end
    local promise = promise_new(name)
    if state then
        local success, result = pcall(
        (state == FULFILLED) and on_fulfilled or on_rejected,
        rawget(self, 'value'), promise)
        if not success then
            promise_reject(promise, result)
        elseif result then
            promise_resolve(promise, result)
        end
    else
        table.insert(self, {promise, on_fulfilled, on_rejected})
    end
    return promise
end
function promise_proto:catch(callback, name) return self:next(nil, callback, name) end
function promise_proto:resolve(value) return promise_resolve(self, value) end
function promise_proto:reject(reason) return promise_reject(self, reason) end
--------------------------------------------------------------------------------
local function promise_foreach(name, race_state, ...)
    local results,chains = {}, {}
    local remaining = select('#', ...)
    local p_array
    if remaining == 1 and type(...) == 'table' then
        p_array = ...
        remaining = #p_array
    else
        p_array = {...}
    end
    local promise = promise_new(name)

    local check_finished = function(i, value, s)
        if not results then return end
        if i then
            results[i] = value
            remaining = remaining - 1
        end
        if remaining == 0 or s then
            if remaining > 0 then
                for i, p in ipairs(chains) do
                    if not rawget(p, 'state') then rawset(p, 'state', s) end
                end
            elseif not s then
                s = FULFILLED
            end
            transition(promise, s, results)
            results = nil
        end
    end

    for i,p in ipairs(p_array) do
        if is_promise(p) then
            table.insert(chains, p:next(
            function(value)  check_finished(i, value, race_state) end,
            function(reason) check_finished(i, reason, REJECTED) end,
            name..i))
        else
            check_finished(i, p, race_state)
        end
    end

    check_finished()
    return promise
end
promise_new = function(callback, name)
    if not name and type(callback) == 'string' then name = callback  end
    local promise = setmetatable({name=name}, promise_mt)
    if is_callable(callback) then 
        callback(
        function(value) promise_resolve(promise, value) end,
        function(reason) promise_reject(promise, reason) end)
    end
    return promise
end
--------------------------------------------------------------------------------
return {
    new = promise_new,
    resolve = function(value, name) return promise_resolve(promise_new(name), value) end,
    reject  = function(value, name) return promise_reject (promise_new(name), value) end,

    race= function(...) return promise_foreach('R', FULFILLED, ...) end,
    all = function(...) return promise_foreach('A', nil, ...) end,

    update = function()
        for i, p in ipairs(promise_queue) do
            promise_queue[i] = nil
            promise_run(p)
        end
    end
}
