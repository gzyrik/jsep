local inputs, opt ={},{}
local global={
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
----------------------------------------------------------------------------------
local arg=...
local ret = dofile('getopt.lua')(arg, 'c,s,m',{
    help='h', map='m', codec='c',
    acodec='c:a', vcodec='c:v',

    h=function(ret, i, x)
        opt.h = x or arg[i+1] or true
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
----------------------------------------------------------------------------------
if opt.h then 
    if type(opt.h) ~= 'string' or not string.match(opt.h, '(%w+)=(%w+)') then
        local man, ret = loadfile('ff-man.lua')
        assert(man, ret)
        man(opt.h)
        os.exit(0)
    end
end
----------------------------------------------------------------------------------
local FFmpeg, ret = loadfile('init.lua')
assert(FFmpeg, ret)
FFmpeg = FFmpeg(opt.sdk or 'Z:/develop/ffmpeg-3.4.2-win64')
local cmd, ret = loadfile('ff-cmd.lua')
assert(cmd, ret)
cmd, ret = pcall(cmd, FFmpeg, opt)
assert(cmd, ret)
if #inputs ==0 then os.exit(0) end
----------------------------------------------------------------------------------
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
            FFmpeg.assert(fmt, file.f)
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
opt.codec_dict = function(opts, codec_id, fmtcxt, stream, codec)
    local ret = ffi.new('AVDictionary*[1]')
    ffi.gc(ret, FFmpeg.av_dict_free)
    local cc = ffi.new('void*[1]', ffi.cast('void*', FFmpeg.avcodec_get_class()))
    local flags = fmtcxt.oformat ~= nil and FFmpeg.AV_OPT_FLAG_ENCODING_PARAM or FFmpeg.AV_OPT_FLAG_DECODING_PARAM
    if not codec then
        if fmtcxt.oformat ~= nil then
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
            local ret = FFmpeg.avformat_match_stream_specifier(fmtcxt, stream, s);
            if ret < 0 then
                FFmpeg.av_log(fmtcxt, FFmpeg.AV_LOG_ERROR, "Invalid stream specifier: %s\n", s)
                os.exit(-1)
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
            FFmpeg.av_log(fmtcxt, FFmpeg.AV_LOG_DEBUG, "codec opts: %s = %s\n", k, v);
        elseif string.sub(k, 1, 1) == prefix and
            nil ~= FFmpeg.av_opt_find(cc, string.sub(k, 2), nil, flags, FFmpeg.AV_OPT_SEARCH_FAKE_OBJ) then
            FFmpeg.av_dict_set(ret, string.sub(k, 2), v, 0)
            FFmpeg.av_log(fmtcxt, FFmpeg.AV_LOG_DEBUG, "codec opts: %s = %s\n", string.sub(k, 2), v);
        end
        ::continue::
    end
    return ret
end
local function filter_map (arg, dft_spec, ret, fmtctx)
    local p1, map,sync,label,sid = 1, arg
    local disabled = string.byte(map, p1) == 126 -- '~' disable
    if disabled then p1 = p1 + 1 end

    local p2 =  string.find(map, ',', p1, true)
    if p2 then --parse sync stream first, just pick first matching stream
        local sync_id, sync_spc = string.match(map, '^(%d*).(.*)$', p2+1)
        map, p1 = string.sub(map, p1, p2-p1), 1
        if not disabled then sync = {sync_id, sync_spc} end
    end
    p2 =  string.find(map, '[', p1, true)
    if p2 then --'[label]' this mapping refers to lavfi output
        label = string.match(map, "^(.*)%]$", p2+1)
        if not label then
            FFmpeg.av_log(nil, FFmpeg.AV_LOG_FATAL, "Invalid output link label: %s.\n", map);
            os.exit(-1)
        end
        if not disabled then
            ret[label] = sync or true
        elseif ret[label] then
            ret[label] = nil
        end
        print('*', label, disabled)
        map, p1 = string.sub(map, p1, p2-p1), 1
    end
    p2 =  string.find(map, '?', p1, true)
    if p2 then
        allow_unused = true
        map, p1 = string.sub(map, p1, p2-p1), 1
    end
    local uidx, spec = string.match(map, '^(%d*)(.*)$', p1)
    uidx = tonumber(uidx)
    if uidx then
        if uidx >= #inputs or uidx < 0 then
            FFmpeg.error("Invalid input file index: %d.\n", uidx)
        end
        if #spec == 0 then
            if type(dft_spec) == 'string' then spec = dft_spec end
        else
            spec = string.sub(spec, 2)
        end
        
        local uid = uidx+1
        local ctx = fmtctx(inputs[uid])--TODO
        for i=0,ctx.nb_streams-1 do
            if FFmpeg.avformat_match_stream_specifier(ctx, ctx.streams[i], spec) > 0 then
                sid = i+1
                if not disabled then
                    if not ret[uid] then ret[uid] = {} end
                    ret[uid][sid] = sync or {uid, sid}
                elseif ret[uid] and ret[uid][sid] then
                    ret[uid][sid] = nil
                    if not next(ret[uid]) then ret[uid] = nil end
                end
                print('*', uid, sid, disabled)
            end
        end
    end
    if not label and not sid then
        if allow_unused or disabled then
            FFmpeg.av_log(nil, FFmpeg.AV_LOG_VERBOSE, "Stream map '%s' matches no streams; ignoring.\n", arg)
        else
            FFmpeg.error("Stream map '%s' matches no streams.\nTo ignore this, add a trailing '?' to the map.\n", arg)
        end
    end
end
local guess_map = function (ofile, fmtctx)
    local m = {}
    if not ofile.vn then
        ofile.vn = FFmpeg.av_guess_codec(fmtctx(ofile).oformat, nil, ofile[-1], nil, FFmpeg.AVMEDIA_TYPE_VIDEO) == FFmpeg.AV_CODEC_ID_NONE
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
            FFmpeg.av_log(nil, FFmpeg.AV_LOG_WARNING, "Guess video map '%s' of '%s' from '%s'\n", val, ofile[-1], inputs[uidx+1][-1])
            table.insert(m, val)
        end
    end
    return m
end
opt.stream_map = function (ofile, fmtctx)
    local maps={}
    if ofile.m then
        for k, v in pairs(ofile.m) do filter_map(v, k, maps, fmtctx) end
    end
    if not next(maps) then
        ofile.m = guess_map(ofile, fmtctx)
        for k, v in pairs(ofile.m) do filter_map(v, k, maps, fmtctx) end
    end
    return next(maps) and maps or nil
end
----------------------------------------------------------------------------------
return FFmpeg, opt, inputs
