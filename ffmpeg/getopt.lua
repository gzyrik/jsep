local function str2tab(str)
    local x={}
    string.gsub(str, '%S+', function(s) table.insert(x, s) end)
    return x
end
local function _getopt(arg, options, repl, start)
    local ret = {}
    if options then
        for k in string.gmatch(options, '(%w):') do ret[k]={} end
    end
    local function setarg(k, x, y)
        if y then
            if not x then return end
            ret[k][y] = x
        elseif type(ret[k]) == 'table' then
            if not x then return end
            table.insert(ret[k], x)
        elseif x then
            if ret[k] == x then return end
            ret[k] = x
        elseif not ret[k] then 
            ret[k] = true
        end
        if repl and type(repl[k]) == 'function' then repl[k](ret) end
    end
    local i, argc = start or 1, #arg
    while i <= argc do
        local v = arg[i]
        if string.byte(v, 1) ~= 45 then -- '-'
            table.insert(ret, v)
            goto continue
        end
        if string.byte(v, 2) == 45 then -- '--'
            local x = string.find(v, '=', 3, true)
            local k = x and string.sub(v, 3, x-1) or string.sub(v, 3)
            if #k == 0 then goto continue end
            if x then
                if x == #v then --codec= aac
                    if i < argc then x, i = arg[i+1], i+1 end
                else
                    x = string.sub(v, x+1)
                end
            end
            if repl and #k > 1 then
                local y = string.find(k, ':', 1, true)
                if not y and repl[k] then
                    k = repl[k]
                    y = string.find(k, ':', 1, true)
                end
                if y and y > 1 and y < #k then
                    local s, y = string.sub(k, 1, y-1), string.sub(k, y+1)
                    if #s > 1 and repl[s] then s = repl[s] end
                    if type(ret[s]) == 'table' then --codec:a=aac
                        setarg(s, x, y)
                        goto continue
                    end
                end
            end
            setarg(k, x)
            goto continue
        end
        for s=2,#v do
            local k = string.sub(v, s, s)
            assert(k ~= ':')
            if options and string.find(options, k, 1, true) then
                local x, y
                if s < #v then
                    s = s + 1
                    if s < #v and string.byte(v, s) == 58 and type(ret[k]) == 'table' then --':'
                        if i < argc then y, x, i = string.sub(v, s+1), arg[i+1], i+1 end
                    else
                        x = string.sub(v, s)
                    end
                elseif i < argc then
                    x, i = arg[i+1], i+1
                end
                setarg(k, x, y)
                goto continue
            end
            setarg(k)
        end
::continue::
        i = i + 1
    end
    if repl then
        for k, v in pairs(repl) do
            if ret[v] and not ret[k] then ret[k] = ret[v] end
        end
    end
    return ret
end
local function getopt(arg, options, repl, start)
    if type(arg) == 'string' then arg = str2tab(arg) end
    assert(type(arg) == 'table')
    --shift
    local i = type(options)
    if i == 'table' then 
        options, repl, start = nil, options, repl
    elseif  i == 'number' then
        options, repl, start = nil, nil, options
    elseif type(repl) == 'number' then
        repl, start = nil, repl
    end
    return _getopt(arg, options, repl, start)
end

if select('#', ...) > 0 then
    local r=getopt('-abc -a')
    assert(r.a == true and r.b and r.c)

    local r=getopt('-a -h', 'h')
    assert(r.a == true and r.h==true)

    local r=getopt('-h -a', 'h')
    assert(not r.a and r.h=='-a')

    local r=getopt('-h type=a', 'h')
    assert(r.h=='type=a')

    local r=getopt('-h type=a -h', 'h:')
    assert(r.h[1]=='type=a' and not r.h[2])

    local r=getopt('-a -b -c', 2)
    assert(not r.a and r.b and r.c)

    local r=getopt('-s:vwxh', 's')
    assert(r.s==':vwxh')

    r=getopt('--codec=aac --pix_fmt= yuv420p',{codec='c'})
    --r={c='aac', pix_fmt=yuv420p}
    assert(r.c == r.codec)
    assert(r.c== 'aac' and r.pix_fmt=='yuv420p')

    r=getopt('-c:v rawvideo --pix_fmt=yuv420p', 'c:')
    --r={c={v='rawvideo'},pix_fmt='yuv420p'}
    assert(r.c.v== 'rawvideo' and r.pix_fmt== 'yuv420p')

    r=getopt('-c -daone -b one -f:x 1 -f:y 2 -f 3 -f0:1 --fz=4','abf:', {fz='f:z'})
    --r={c=true,d=true,a='one',b='one',f={1='3',2='0:1',x='1',y='2',z='4'}}
    assert(r.c==true and r.d==true and r.a=='one' and r.b=='one')
    assert(r.f[1]=='3' and r.f[2]=='0:1' and r.f.x=='1' and r.f.y=='2' and r.f.z=='4')

    local f={}
    r=getopt('-i a -f B -i b --input=c', 'i:f', {input='i',i=function(r) f[#r.i], r.f = r.f, nil end})
    --r={i={a, b}}, f={nil, B}
    assert(r.input == r.i)
    assert(r.i[1]=='a' and r.i[2]=='b' and r.i[3]=='c' and not r.f)
    assert(not f[1] and f[2]=='B' and not f[3])
    print('test ok')
end
return getopt
