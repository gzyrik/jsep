local CWD, ARG =...
--------------------------------------------------------------------------------
local inputs, opt ={},{}
local global={'sdk','cdef',
    'y','n','hide_banner', 'v',
    'protocols', 'filters', 'pix_fmts',
    'codecs', 'decoders', 'encoders',
    'formats', 'muxers', 'demuxers', 'devices',
}
local function perfile(ret, x)
    for _, k in ipairs(global) do
        if ret[k] then
            opt[k] = ret[k]
            ret[k] = nil
        end
    end
    if x then ret[-1] = x end
    return ret
end
local function url(file) return file and file[-1] end
--------------------------------------------------------------------------------
local ret = dofile(CWD..'getopt.lua')(ARG, 'c,s,m,filter',{
    help='h', map='m', codec='c',
    acodec='c:a', vcodec='c:v',
    vf='filter:v', af='filter:a',

    h=function(ret, i, x)
        opt.h = x or ARG[i+1] or true
        return ret
    end,
    i=function(ret, i, x)
        table.insert(inputs, perfile(ret, x))
        return {}
    end,
    ['-']=function(ret, i, x)
        table.insert(opt, perfile(ret, x))
        return {}
    end,
})
ret = perfile(ret)
if next(ret) then table.insert(opt, ret) end
if not next(opt) and #inputs == 0 then opt.h = 'demo' end
--------------------------------------------------------------------------------
if opt.h then 
    if type(opt.h) ~= 'string' or not string.match(opt.h, '(%w+)=(%w+)') then
        local man, ret = loadfile(CWD..'ff-man.lua')
        assert(man, ret)
        man(opt.h)
        os.exit(0)
    end
end
local SEP = string.sub(package.config, 1, 1)
-- read cached sdk list from sdk.txt
local sdk_list={}
for line in io.lines(CWD..'sdk.txt') do 
    local file
    local suffix = string.match(package.cpath, '.*(%..*)')
    if SEP == '\\' then
        line = string.gsub(line, '/', SEP)
        file = io.popen('dir /B '..line..'bin\\*.dll 2>NUL')
    else
        file = io.popen('ls '..line..'bin/*'..suffix)
    end
    if string.match(file:read('*all'), '.*%'..suffix) then
        table.insert(sdk_list, 1, line)
    else
        table.insert(sdk_list, line)
    end
    file:close()
end
if not opt.sdk then opt.sdk, sdk_list = sdk_list[1] end
assert(opt.sdk, "\nUse -sdk to set ffmpeg sdk dir, run '-h -sdk' for help")
if string.sub(opt.sdk, -1) ~= SEP then opt.sdk = opt.sdk .. SEP end
if not opt.cdef then opt.cdef = CWD .. string.match(opt.sdk, '[_-](%d*%.%d*)')..'.cdef' end
--------------------------------------------------------------------------------
local FFmpeg, ret = loadfile(CWD..'init.lua')
assert(FFmpeg, ret)
FFmpeg = FFmpeg(opt.sdk, opt.cdef, CWD)
if sdk_list then -- insert sdk to sdk.txt
    table.insert(sdk_list, 1, opt.sdk)
    local file = io.open(CWD..'sdk.txt', 'w')
    for i, v in ipairs(sdk_list) do -- remove duplicate
        if not sdk_list[v] then file:write(v, '\n') end
        sdk_list[v] = i
    end
    sdk_list = nil
    file:close()
end
local cmd, ret = loadfile(CWD..'ff-cmd.lua')
assert(cmd, ret)
cmd, ret = pcall(cmd, FFmpeg, opt)
assert(cmd, ret)
if #inputs ==0 then os.exit(0) end
--------------------------------------------------------------------------------
local ffi, bit = require'ffi', require'bit'
local function prepare(files, input)
    for _, file in ipairs(files) do
        if file.pix_fmt then
            local pix_fmt = FFmpeg.av_get_pix_fmt(file.pix_fmt)
            assert(pix_fmt ~= FFmpeg.AV_PIX_FMT_NONE, file.pix_fmt)
            file.pix_fmt = pix_fmt
        end
        if file.f and input then
            local fmt = FFmpeg.av_find_input_format(file.f)
            FFmpeg.assert(fmt, "Unknown input format: '%s'\n", file.f)
            file.f = fmt
        end
        if file.c then
            if file.c.v then
                local codec
                if input then
                    codec = FFmpeg.avcodec_find_decoder_by_name(file.c.v)
                else
                    codec = FFmpeg.avcodec_find_encoder_by_name(file.c.v)
                end
                assert(codec ~= nil, file.c.v)
                file.c.v = codec.id
            end
        end
    end
end
prepare(inputs, true)
prepare(opt, false)
--------------------------------------------------------------------------------
local function format_dict(opts)
    local ret = ffi.new('AVDictionary*[1]')
    ffi.gc(ret, FFmpeg.av_dict_free)
    local cc = ffi.new('void*[1]', ffi.cast('void*', FFmpeg.avformat_get_class()))
    for k, v in pairs(opts) do
        if type(k) ~= 'string' or type(v) ~= 'string' then goto continue end

        if nil ~= FFmpeg.av_opt_find(cc, k, nil, 0,
            bit.bor(FFmpeg.AV_OPT_SEARCH_CHILDREN, FFmpeg.AV_OPT_SEARCH_FAKE_OBJ)) then
            FFmpeg.av_dict_set(ret, k, v, 0)
            FFmpeg.av_log(nil, FFmpeg.AV_LOG_DEBUG, "format opts: %s = %s\n", k, v);
        end
        ::continue::
    end
    return ret
end
local function fmtctx(file, output)
    if file[0] then return file[0].fmtctx[0] end
    local name, ctx = url(file), ffi.new('AVFormatContext*[1]')
    ffi.gc(ctx, FFmpeg.avformat_close_input)

    file[0] = { fmtctx = ctx }
    if output then
        local ret = FFmpeg.avformat_alloc_output_context2(ctx, nil, file.f,  name)
        FFmpeg.assert(ret, name)
    else
        for _, ofile in ipairs(opt) do
            if ofile == file then error('output file as input', 2) end
        end
        local ret = FFmpeg.avformat_open_input(ctx, name, file.f, format_dict(file))
        FFmpeg.assert(ret, name)

        local ret = FFmpeg.avformat_find_stream_info(ctx[0], nil)
        FFmpeg.assert(ret, name)

        FFmpeg.av_dump_format(ctx[0], 0, name, 0)
    end
    return file[0].fmtctx[0]
end
local function codec_dict (opts, codec_id, ctx, stream, codec)
    local ret = ffi.new('AVDictionary*[1]')
    ffi.gc(ret, FFmpeg.av_dict_free)
    local cc = ffi.new('void*[1]', ffi.cast('void*', FFmpeg.avcodec_get_class()))
    local flags = ctx.oformat ~= nil and FFmpeg.AV_OPT_FLAG_ENCODING_PARAM or FFmpeg.AV_OPT_FLAG_DECODING_PARAM
    if not codec then
        if ctx.oformat ~= nil then
            codec =  FFmpeg.avcodec_find_encoder(codec_id)
        else
            codec =  FFmpeg.avcodec_find_decoder(codec_id)
        end
    end
    local priv_class
    if codec ~= nil and codec.priv_class ~= nil then
        priv_class = ffi.new('void*[1]', ffi.cast('void*', codec.priv_class))
    end
    local prefix
    if stream.codecpar.codec_type == FFmpeg.AVMEDIA_TYPE_VIDEO then
        prefix  = 'v';
        flags  = bit.bor(flags, FFmpeg.AV_OPT_FLAG_VIDEO_PARAM)
    elseif  stream.codecpar.codec_type == FFmpeg.AVMEDIA_TYPE_AUDIO then
        prefix  = 'a';
        flags  = bit.bor(flags, FFmpeg.AV_OPT_FLAG_AUDIO_PARAM)
    elseif  stream.codecpar.codec_type == FFmpeg.AVMEDIA_TYPE_SUBTITLE then
        prefix  = 's';
        flags  = bit.bor(flags, FFmpeg.AV_OPT_FLAG_SUBTITLE_PARAM)
    end
    for k, v in pairs(opts) do
        if type(k) ~= 'string' or type(v) ~= 'string' then goto continue end
        local p = string.find(k, ':', 1, true)
        -- check stream specification in opt name
        if p then
            local s = string.sub(k, p+1)
            local ret = FFmpeg.avformat_match_stream_specifier(ctx, stream, s);
            if ret < 0 then
                FFmpeg.error(ctx, "Invalid stream specifier: %s\n", s)
            elseif ret == 0 then
                goto continue --skip
            else
                k = string.sub(k, 1, p-1)
            end
        end
        if nil ~= FFmpeg.av_opt_find(cc, k, nil, flags, FFmpeg.AV_OPT_SEARCH_FAKE_OBJ) or
            (priv_class and 
            nil ~= FFmpeg.av_opt_find(priv_class, k, nil, flags, FFmpeg.AV_OPT_SEARCH_FAKE_OBJ)) then
            FFmpeg.av_dict_set(ret, k, v, 0)
            FFmpeg.av_log(ctx, FFmpeg.AV_LOG_DEBUG, "codec opts: %s = %s\n", k, v);
        elseif string.sub(k, 1, 1) == prefix and
            nil ~= FFmpeg.av_opt_find(cc, string.sub(k, 2), nil, flags, FFmpeg.AV_OPT_SEARCH_FAKE_OBJ) then
            FFmpeg.av_dict_set(ret, string.sub(k, 2), v, 0)
            FFmpeg.av_log(ctx, FFmpeg.AV_LOG_DEBUG, "codec opts: %s = %s\n", string.sub(k, 2), v);
        end
        ::continue::
    end
    return ret
end
local function for_stream(name, dft_spec)
    local uidx, spec = string.match(name, '^(%d*)(.*)$')
    uidx = tonumber(uidx)
    if uidx >= #inputs or uidx < 0 then
        FFmpeg.error("Invalid input file index: %d.\n", uidx)
    end
    if #spec == 0 then
        if type(dft_spec) == 'string' then spec = dft_spec end
    elseif string.byte(spec, 1) == 58 then -- ':'
        spec = string.sub(spec, 2)
    end
    local uid = uidx+1
    local ctx = fmtctx(inputs[uid])
    return function(ctx, sidx)
        while sidx < ctx.nb_streams do
            if FFmpeg.avformat_match_stream_specifier(ctx, ctx.streams[sidx], spec) > 0  then
                return sidx+1, uid, ctx.streams[sidx]
            end
            sidx = sidx + 1
        end
    end, ctx, 0
end
local function parse_stream_map (map_desc, dft_spec, maps)
    local p1, desc,sync,label,has_sid = 1, map_desc
    local disabled = string.byte(desc, p1) == 126 -- '~' disable
    if disabled then p1 = p1 + 1 end

    local p2 =  string.find(desc, ',', p1, true)
    if p2 then --parse sync stream first, just pick first matching stream
        local sync_id, sync_spc = string.match(desc, '^(%d*).(.*)$', p2+1)
        desc, p1 = string.sub(desc, p1, p2-p1), 1
        if not disabled then sync = {sync_id, sync_spc} end
    end
    p2 =  string.find(desc, '[', p1, true)
    if p2 then --'[label]' this mapping refers to lavfi output
        label = string.match(desc, "^(.*)%]$", p2+1)
        if not label then FFmpeg.error("Invalid output link label: %s.\n", desc) end
        if not disabled then
            maps[label] = sync or true
        elseif maps[label] then
            maps[label] = nil
        end
        --print('*label', label, disabled)
        desc, p1 = string.sub(desc, p1, p2-p1), 1
    end
    p2 =  string.find(desc, '?', p1, true)
    if p2 then
        allow_unused = true
        desc, p1 = string.sub(desc, p1, p2-p1), 1
    end
    for sid, uid in for_stream(string.sub(desc, p1), dft_spec) do
        has_sid = true
        if not disabled then
            if not maps[uid] then maps[uid] = {} end
            assert(not maps[uid][sid])
            maps[uid][sid] = sync or {uid, sid}
            --print('*map', uid, sid)
        elseif ret[uid] and maps[uid][sid] then
            maps[uid][sid] = nil
            --print('*umap', uid, sid)
            if not next(maps[uid]) then maps[uid] = nil end
        end
    end
    if not label and not has_sid then
        if allow_unused or disabled then
            FFmpeg.av_log(nil, FFmpeg.AV_LOG_VERBOSE,
            "Stream map '%s' matches no streams; ignoring.\n", map_desc)
        else
            FFmpeg.error("Stream map '%s' matches no streams.\n"..
            "To ignore this, add a trailing '?' to the map.\n", map_desc)
        end
    end
end
local guess_map = function (ofile)
    local m = {}
    if not ofile.vn then
        ofile.vn = FFmpeg.av_guess_codec(fmtctx(ofile).oformat, nil, url(ofile), nil, FFmpeg.AVMEDIA_TYPE_VIDEO) == FFmpeg.AV_CODEC_ID_NONE
    end
    if not ofile.vn then
        local best_area, uidx, sidx=0
        for i, ifile in ipairs(inputs) do -- find best video: highest resolution
            local sindex = FFmpeg.av_find_best_stream(fmtctx(ifile), FFmpeg.AVMEDIA_TYPE_VIDEO, -1, -1, nil, 0);
            if sindex >= 0  then
                local st = fmtctx(ifile).streams[sindex]
                local area = st.codecpar.width * st.codecpar.height
                if area > best_area then
                    best_area, uidx, sidx = area, i-1, sindex
                end
            end
        end
        if sidx then
            local val = uidx..':'..sidx
            FFmpeg.av_log(fmtctx(ofile), FFmpeg.AV_LOG_WARNING,
            "Guess video map '%s' from '%s'\n", val, url(inputs[uidx+1]))
            table.insert(m, val)
        end
    end
    return m
end
local function find_stream_by_filter(input, dft_spec, graph_desc)
    local mtype = FFmpeg.avfilter_pad_get_type(input.filter_ctx.input_pads, input.pad_idx)
    if  input.name ~= nil then
        local name = ffi.string(input.name)
        for sid, uid, st in for_stream(name, dft_spec) do
            if st.codecpar.codec_type == mtype then return uid, sid end
        end
        FFmpeg.error("Stream specifier '%s' in filtergraph description %s "..
        "matches no streams.\n", name, graph_desc)
    else -- find the first unused stream of corresponding type
        for uid, ifile in ipairs(inputs) do
            local ctx, off = fmtctx(ifile), #ifile
            for i=0, ctx.nb_streams-1 do
                local sidx = (i + off) % ctx.nb_streams
                if ctx.streams[sidx].codecpar.codec_type == mtype and (type(dft_spec) ~= 'string' or
                    FFmpeg.avformat_match_stream_specifier(ctx, ctx.streams[sidx], dft_spec) > 0) then
                    FFmpeg.av_log(nil, FFmpeg.AV_LOG_WARNING,
                    "Guess filter map '%s' from '%s'\n", (uid-1)..':'..sidx, url(ifile))
                    return uid, sidx+1
                end
            end
        end
        FFmpeg.error("Cannot find a matching stream for "..
        "unlabeled input pad %d on filter %s\n", input.pad_idx, ffi.string(input.filter_ctx.name))
    end
end
local function parse_filter_map(graph_desc, dft_spec, maps)
    local simple = true
    local graph = ffi.new('AVFilterGraph*[1]')
    ffi.gc(graph, FFmpeg.avfilter_graph_free)
    local inputs, outputs= ffi.new('AVFilterInOut*[1]'),ffi.new('AVFilterInOut*[1]')
    ffi.gc(inputs, FFmpeg.avfilter_inout_free)
    ffi.gc(outputs, FFmpeg.avfilter_inout_free)

    graph[0] = FFmpeg.avfilter_graph_alloc()
    local ret = FFmpeg.avfilter_graph_parse2(graph[0], graph_desc, inputs, outputs)
    FFmpeg.assert(ret, 'avfilter_graph_parse2')
    if simple then
        if outputs[0] == nil then
            num_outputs = '0'
        elseif outputs[0].next ~= nil then
            num_outputs = '>1'
        else
            num_outputs = '1'
        end
        if inputs[0] == nil then
            num_inputs = '0'
        elseif inputs[0].next ~= nil then
            num_inputs = '>1'
        else
            num_inputs = '1'
        end
        if num_outputs ~='1' and num_inputs ~= '1' then
            FFmpeg.error("Simple filtergraph '%s' was expected "..
            "to have exactly 1 input and 1 output."..
            " However, it had %s input(s) and %s output(s)."..
            " Please adjust, or use a complex filtergraph (-filter_complex) instead.\n",
            graph_desc, num_inputs, num_outputs)
        end
    end
    local cur, i = inputs[0], 0
    while cur ~= nil do
        local uid, sid = find_stream_by_filter(cur, dft_spec, graph_desc)
        if not maps[uid] then maps[uid] = {} end
        assert(not maps[uid][sid])
        maps[uid][sid] = {filter={graph=graph, inputs=inputs, outputs=outputs}}
        --print('*graph', uid, sid)
        cur = cur.next
        i = i + i
    end
end
local function stream_map (ofile)
    local maps={}
    if ofile.m then
        for k, v in pairs(ofile.m) do parse_stream_map(v, k, maps) end
    end
    if ofile.filter then
        for k, v in pairs(ofile.filter) do parse_filter_map(v, k, maps) end
    elseif not next(maps) then
        ofile.m = guess_map(ofile)
        for k, v in pairs(ofile.m) do parse_stream_map(v, k, maps) end
    end
    return maps
end
local function choose_pix_fmt(codec, target)
    local p = codec.pix_fmts
    local desc = FFmpeg.av_pix_fmt_desc_get(target);
    local has_alpha = desc ~= nil and bit.band(desc.nb_components, 1) == 0
    local best= FFmpeg.AV_PIX_FMT_NONE
    while p[0] ~= FFmpeg.AV_PIX_FMT_NONE do
        if p[0] == target then return target end
        best = FFmpeg.avcodec_find_best_pix_fmt_of_2(best, p[0], target, has_alpha, nil)
        p = p + 1
    end
    if p[0] == FFmpeg.AV_PIX_FMT_NONE then
        if target ~= FFmpeg.AV_PIX_FMT_NONE then
            FFmpeg.av_log(nil, FFmpeg.AV_LOG_WARNING,
            "Incompatible pixel format '%s' for codec '%s', auto-selecting format '%s'\n",
            FFmpeg.av_get_pix_fmt_name(target),
            codec.name,
            FFmpeg.av_get_pix_fmt_name(best))
            return best
        end
    end
    return target
end
--------------------------------------------------------------------------------
opt.url = url
opt.fmtctx = fmtctx
opt.stream_map = stream_map
opt.codec_dict = codec_dict
opt.choose_pix_fmt = choose_pix_fmt
return FFmpeg, opt, inputs
