local function str2tab(str)
    local x={}
    string.gsub(str, '%S+', function(s) table.insert(x, s) end)
    return x
end
local function _getopt(arg, options, repl, start)
    local ret = {}
    local mul ={}
    if options then
        for k in string.gmatch(options, '(%w+)') do mul[k]=true end
    end
    local i, argc = start, #arg
    local function setarg(k, x, y)
        if repl and type(repl[k]) == 'function' then
            ret = repl[k](ret, i, x, y)
        elseif k == '-' then
            table.insert(ret, x)
        elseif mul[k] then
            if not ret[k] then ret[k] = {} end
            if y then
                ret[k][y] = x
            else
                table.insert(ret[k], x or true)
            end
        elseif x then
            if ret[k] == x then return end
            ret[k] = x
        elseif not ret[k] then 
            ret[k] = true
        end
    end
    local function langarg(v, idx)
        local x = string.find(v, '=', idx, true)
        local k = x and string.sub(v, idx, x-1) or string.sub(v, idx)
        if #k == 0 then return end
        if x and x < #v then
            x = string.sub(v, x+1)
        elseif i < argc and string.byte(arg[i+1], 1) ~= 45 then
            x, i = arg[i+1], i+1
        else
            x = nil
        end
        local y = string.find(k, ':', 1, true)
        if repl and not y and type(repl[k]) == 'string' then
            k = repl[k]
            y = string.find(k, ':', 1, true)
        end
        if y and y > 1 and y < #k then
            local s, y = string.sub(k, 1, y-1), string.sub(k, y+1)
            if repl and #s > 1 and type(repl[s]) == 'string' then s = repl[s] end
            if mul[s] then return setarg(s, x, y) end
        end
        return setarg(k, x)
    end
    while i <= argc do
        local v = arg[i]
        if string.byte(v, 1) ~= 45 then -- '-'
            setarg('-', v)
        elseif string.byte(v, 2) == 45 then -- '--'
            langarg(v, 3)
        else -- '-'
            langarg(v, 2)
        end
        i = i + 1
    end
    if repl then
        for k, v in pairs(repl) do
            if not ret[k] and type(v) == 'string' and type(ret[v]) == 'table' then
                ret[k] = ret[v]
            end
        end
    end
    return ret
end
local function getopt(arg, options, repl, start)
    if type(arg) == 'string' then arg = str2tab(arg) end
    assert(type(arg) == 'table')
    --shift
    local t = type(options)
    if t == 'table' then 
        options, repl, start = nil, options, repl
    elseif  t == 'number' then
        options, repl, start = nil, nil, options
    else
        if t ~= 'string' then options = nil end
        t = type(repl)
        if t == 'number' then
            repl, start = nil, repl
        elseif t ~= 'table' then
            repl = nil
        end
        if type(start) ~= 'number' then start = 1 end
    end
    return _getopt(arg, options, repl, start)
end

if select('#', ...) > 0 then
    local r=getopt('-abc -a --bcd') -- '-' is equal to '--'
    assert(r.abc == true and r.a == true and r.bcd == true and not r.c)

    local r=getopt('-abc a -abc') -- ignore not first null 
    assert(r.abc == 'a')

    local r=getopt('--abc a -bcd=a -h= all') -- two '=' formats
    assert(r.bcd=='a' and r.h == 'all' and r.abc == 'a')
    local r=getopt('--codec=aac --pix_fmt= yuv420p')
    assert(r.codec== 'aac' and r.pix_fmt=='yuv420p')

    local r=getopt('-h type=a') -- not '=' format
    assert(r.h=='type=a')

    local r=getopt('-h -a -h b', 'h') -- array-format, the first null is valid
    assert(r.h[1]== true and r.h[2] == 'b' and r.a == true)

    local r=getopt('-h type=a -h', 'h')--array-format, the second null is valid
    assert(r.h[1]=='type=a' and r.h[2] == true)

    local r=getopt('-a -b -c', 2) -- start index
    assert(not r.a and r.b == true and r.c == true)

    local r=getopt('-s:v wxh -s:a', 's') -- map-format, null is ignore
    assert(r.s.v=='wxh' and not r.s.a)

    r=getopt('-c:v rawvideo --pix_fmt=yuv420p', 'c') -- map-format and '=' format
    assert(r.c.v=='rawvideo' and r.pix_fmt=='yuv420p')

    r=getopt('-c -daone -b one -f:x 1 -f:y 2 -f 3 -f 0:1 --fz=4','f', {fz='f:z'})-- map-format, nick-format
    assert(r.c==true and r.daone==true and not r.a and r.b=='one')
    assert(r.f[1]=='3' and r.f[2]=='0:1' and r.f.x=='1' and r.f.y=='2' and r.f.z=='4' and not r.fz)

    local f={}
    r=getopt('-i a -f B -i b --input=c', 'i', {
        input='i',
        i=function(r) f[#r.i], r.f = r.f, nil end
    }) -- collect per input options
    assert(r.input == r.i)
    assert(r.i[1]=='a' and r.i[2]=='b' and r.i[3]=='c' and not r.f)
    assert(not f[1] and f[2]=='B' and not f[3])
    print('test ok')
    --for k,v in pairs(f) do print(k, v) end
end
return getopt
