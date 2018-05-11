local ffi, bit = require'ffi', require'bit'
local _OPT, ret = loadfile('ff-opt.lua')
assert(_OPT, ret)
local ret, FFmpeg, _OPT, _URL = pcall(_OPT, arg)
assert(ret, FFmpeg)
--_OPT表: Hash部分为全局参数, Array部分为输出文件`FILE表'
--_URL表: Array部分为输入文件`FILE表'
--`FILE表': Hash部分为参数集合
--      [-1] 为url
--      [0] 为该文件`运行态资源'{
--          fmtctx=AVFormatContext**,
--      }
--      [...]为该文件中流AVStream.index+1 对应的`编解码单元' {
--          avctx=AVCodecContext*,
--          frame=AVFrame*,
--          packet=AVPacket*,
--          //编码特有,对应解码器=_URL[uid][sid]
--          uid=<uint>, 输入源在_URL中的序号,[1,#_URL]
--          sid=<uint>, 输入源文件中的流序号,[1,fmtctx.nb_streams]
--          swsctx=SwsContext*,
--      }
local function url(file) return file and file[-1] end
local function fmtctx(file)
    if file[0] then return file[0].fmtctx[0] end
    for _, ofile in ipairs(_OPT) do
        if ofile == file then error('input file as output', 2) end
    end
    --open input file
    local name = url(file)
    assert(not file[0], name)
    file[0] = { fmtctx = ffi.new('AVFormatContext*[1]') }
    local ret = FFmpeg.avformat_open_input(file[0].fmtctx, name, file.f, nil)--TODO options
    FFmpeg.assert(ret, name)

    local ret = FFmpeg.avformat_find_stream_info(fmtctx(file), nil)
    FFmpeg.assert(ret, name)

    FFmpeg.av_dump_format(fmtctx(file), 0, name, 0)
    return file[0].fmtctx[0]
end
----------------------------------------------------------------------------------
local function open_stream(uid, sid)
    local ifile = _URL[uid]
    local ist = fmtctx(ifile).streams[sid-1]
    if ifile[sid] then return ist end

    local ipar = ist.codecpar
    local codec = FFmpeg.avcodec_find_decoder(ipar.codec_id)
    local avctx = FFmpeg.avcodec_alloc_context3(codec);
    local ret = FFmpeg.avcodec_parameters_to_context(avctx, ipar)
    FFmpeg.assert(ret, url(ifile))

    local dict = _OPT.codec_dict(ifile, ipar.codec_id, fmtctx(ifile), ist, codec)
    local ret = FFmpeg.avcodec_open2(avctx, codec, dict)
    FFmpeg.assert(ret, url(ifile))

    assert(ist.index+1 == sid)
    ifile[sid] = {
        avctx = avctx,
        frame = FFmpeg.av_frame_alloc(),
        packet = FFmpeg.av_packet_alloc(),
    }
    return ist
end
local function add_stream(ofile, uid, sid)
    local ist = open_stream(uid, sid)
    local ipar = ist.codecpar
    local ost = FFmpeg.avformat_new_stream(fmtctx(ofile), nil)
    local opar = ost.codecpar
    ost.time_base.num, ost.time_base.den = ist.time_base.num, ist.time_base.den
    opar.codec_type = ipar.codec_type
    opar.format = ofile.pix_fmt  or ipar.format
    if ofile.c and ofile.c.v then
        opar.codec_id = ofile.c.v
    else
        opar.codec_id = ipar.codec_id
    end
    if ofile.s and ofile.s.v then
        local w, h = string.match(ofile.s.v, '^(%d*)x(%d*)$')
        assert(w and h, ofile.s.v)
        opar.width, opar.height = tonumber(w), tonumber(h)
    else
        opar.width, opar.height = ipar.width, ipar.height
    end
    --创建编码的AVCodecContext
    local codec = FFmpeg.avcodec_find_encoder(opar.codec_id)
    local avctx = FFmpeg.avcodec_alloc_context3(codec)
    avctx.time_base.num, avctx.time_base.den = ost.time_base.num, ost.time_base.den
    local ret = FFmpeg.avcodec_parameters_to_context(avctx, opar)
    FFmpeg.assert(ret, 'avcodec_parameters_to_context')
    local dict = _OPT.codec_dict(ofile, opar.codec_id, fmtctx(ofile), ost, codec)
    local ret = FFmpeg.avcodec_open2(avctx, codec, dict)
    FFmpeg.assert(ret, 'avcodec_open2')
    ret = FFmpeg.avcodec_parameters_from_context(opar, avctx)
    FFmpeg.assert(ret, 'avcodec_parameters_from_context')
    local frame = FFmpeg.av_frame_alloc();
    frame.format = opar.format;
    frame.width  = opar.width;
    frame.height = opar.height;
    FFmpeg.av_frame_get_buffer(frame, 32);
    local swsctx = FFmpeg.sws_getCachedContext(nil,
        ipar.width, ipar.height, ipar.format,
        opar.width, opar.height, opar.format,
        FFmpeg.SWS_BICUBIC, nil, nil, nil)

    ofile[ost.index+1] = {
        avctx = avctx,
        swsctx = swsctx,
        frame = frame,
        packet = FFmpeg.av_packet_alloc(),
        uid=uid,
        sid=sid,
    }
end
local function open_ofile(ofile)
    local name = url(ofile)
    assert(not ofile[0], name)
    ofile[0] = { fmtctx = ffi.new('AVFormatContext*[1]') }
    local ret = FFmpeg.avformat_alloc_output_context2(ofile[0].fmtctx, nil, ofile.f,  name)
    FFmpeg.assert(ret, name)

    ofile.m = _OPT.stream_map(ofile, fmtctx)
    for uid, st in ipairs(ofile.m) do
        for sid, sync in ipairs(st) do add_stream(ofile, uid, sid) end
    end
    FFmpeg.av_dump_format(fmtctx(ofile), 0, name, 1)

    --open the output url, if needed
    if bit.band(fmtctx(ofile).oformat.flags, FFmpeg.AVFMT_NOFILE) == 0 then 
        if not _OPT.y and io.open(name, 'r') then
            if _OPT.n then
                FFmpeg.av_log(nil, FFmpeg.AV_LOG_ERROR,
                "File '%s' already exists. Exiting.", name)
                os.exit(0)
            else
                FFmpeg.av_log(nil, FFmpeg.AV_LOG_ERROR,
                "File '%s' already exists. Overwrite ? [y/N]", name)
                if io.read('*l') ~= 'y' then os.exit(0) end
            end
        end
        local pb = ffi.new('AVIOContext*[1]')
        ret = FFmpeg.avio_open(pb, name, FFmpeg.AVIO_FLAG_WRITE)
        assert(ret, name)
        fmtctx(ofile).pb = pb[0]
    end
    local ret = FFmpeg.avformat_write_header(fmtctx(ofile), nil)
    FFmpeg.assert(ret, name)
end
local function choose_output_file()
    for _, ofile in ipairs(_OPT) do
        if ofile[0] then return ofile end
    end
end
local function choose_ocell(ofile)
    for _, cell in ipairs(ofile) do
        if cell.avctx then return cell end
    end
end
local function close_ocell(ocell)
    if not ocell.avctx then return end
    FFmpeg.avcodec_free_context(ffi.new('AVCodecContext*[1]', ocell.avctx))
    ocell.avctx = nil
    FFmpeg.av_frame_free(ffi.new('AVFrame*[1]', ocell.frame))
    ocell.frame = nil
    FFmpeg.av_packet_free(ffi.new('AVPacket*[1]', ocell.packet))
    ocell.packet = nil
end
local function close_ofile(ofile)
    if not ofile[0] then return end
    local fmtctx = fmtctx(ofile)
    for _, ocell in ipairs(ofile) do close_ocell(ocell) end
    local ret = FFmpeg.av_write_trailer(fmtctx)
    FFmpeg.assert(ret, url(ofile))
    if fmtctx.pb then 
        FFmpeg.avio_closep(ffi.new('AVIOContext*[1]', fmtctx.pb))
        fmtctx.pb = nil
    end
    FFmpeg.avformat_close_input(ofile[0].fmtctx)
    ofile[0] = nil
end
local function close_ifile(ifile)
    if not ifile[0] then return end
    for _, icell in ipairs(ifile) do
        FFmpeg.avcodec_free_context(ffi.new('AVCodecContext*[1]', icell.avctx))
        icell.avctx = nil
        FFmpeg.av_frame_free(ffi.new('AVFrame*[1]', icell.frame))
        icell.frame = nil
        FFmpeg.av_packet_free(ffi.new('AVPacket*[1]', icell.packet))
        icell.packet = nil
    end
    FFmpeg.avformat_close_input(ifile[0].fmtctx)
    ifile[0] = nil
end
--receive frame frome the peer decoder
local function receive_frame(ocell)
    local ifile = _URL[ocell.uid]
    local icell = ifile[ocell.sid]
    local ret = 0
    while ret == 0 and not icell.receiving do
        ret = FFmpeg.av_read_frame(fmtctx(ifile), icell.packet)
        if ret == 0 and icell.packet.stream_index + 1 == ocell.sid then
            ret = FFmpeg.avcodec_send_packet(icell.avctx, icell.packet)
            if ret == 0 then icell.receiving = true end
        end
    end
    if ret == 0 then
        ret = FFmpeg.avcodec_receive_frame(icell.avctx, icell.frame)
        if ret ~= 0 then icell.receiving = nil end
    end
    return ret, icell.frame
end
local function transcode_step(ofile)
    local ocell = choose_ocell(ofile)
    if not ocell then return close_ofile(ofile) end
    while true do
        local ret, frame = receive_frame(ocell)
        if ret ~= 0 then 
            if ret == FFmpeg.AVERROR_EOF then 
                close_ocell(ocell)
            else
                assert(ret == FFmpeg.AVERROR_AGAIN, 'decode_frame')
            end
            return
        end
        FFmpeg.sws_scale(ocell.swsctx, ffi.cast('const unsigned char *const*', frame.data),
        frame.linesize, 0, ocell.avctx.height, ocell.frame.data, ocell.frame.linesize)
        ret = FFmpeg.avcodec_send_frame(ocell.avctx, ocell.frame)
        while ret == 0 do
            ret = FFmpeg.avcodec_receive_packet(ocell.avctx, ocell.packet);
            if ret == 0 then 
                ret = FFmpeg.av_write_frame(fmtctx(ofile), ocell.packet);
                if ret == FFmpeg.AVERROR_EIO then
                    return close_ofile(ofile)
                else
                    FFmpeg.assert(ret, 'av_write_frame')
                end
            else
                assert(ret == FFmpeg.AVERROR_AGAIN, 'avcodec_receive_packet')
            end
        end
    end
end
----------------------------------------------------------------------------------
local _TTY, ret = loadfile('ff-tty.lua')
assert(_TTY, ret)
ret, _TTY = pcall(_TTY, FFmpeg, _OPT)
assert(ret, _TTY)
----------------------------------------------------------------------------------
local function transcode()
    for _, ofile in ipairs(_OPT) do open_ofile(ofile) end
    while not _TTY.sigterm do
        local cur_time= FFmpeg.av_gettime_relative()
        if _TTY.check(cur_time) < 0 then break end

        local ofile = choose_output_file()
        if not ofile then break end

        transcode_step(ofile)
    end
    for _, ifile in ipairs(_URL) do close_ifile(ifile) end
    for _, ofile in ipairs(_OPT) do close_ofile(ofile) end
end
----------------------------------------------------------------------------------
local function open_output(i_stm, opt)
    local fmt_ctx = ffi.new('AVFormatContext*[1]')
    local ret = FFmpeg.avformat_alloc_output_context2(fmt_ctx, nil, opt.f,  opt[0])
    FFmpeg.assert(ret, 'avformat_alloc_output_context2')
    local stream = FFmpeg.avformat_new_stream(fmt_ctx[0], nil)
    stream.time_base.num = 1
    stream.time_base.den = 25
    local codecpar = stream.codecpar

    codecpar.codec_type = i_stm.codecpar.codec_type
    codecpar.format = opt.pix_fmt  or i_stm.codecpar.format
    codecpar.codec_id = opt.c.v or i_stm.codecpar.codec_id
    codecpar.width = i_stm.codecpar.width
    codecpar.height = i_stm.codecpar.height
    if opt.s and opt.s.v then
        local w, h = string.match(opt.s.v, '^(%d*)x(%d*)$')
        assert(w and h, opt.s.v)
        codecpar.width, codecpar.height = tonumber(w), tonumber(h)
    end
    --创建编码的AVCodecContext
    local codec = FFmpeg.avcodec_find_encoder(codecpar.codec_id)
    local codec_cxt = FFmpeg.avcodec_alloc_context3(codec);
    codec_cxt.time_base = stream.time_base
    local ret = FFmpeg.avcodec_parameters_to_context(codec_cxt, codecpar)
    FFmpeg.assert(ret, 'avcodec_parameters_to_context')
    local dict = _OPT.codec_dict(opt, codecpar.codec_id, fmt_ctx[0], stream, codec)
    local ret = FFmpeg.avcodec_open2(codec_cxt, codec, dict)
    FFmpeg.assert(ret, 'avcodec_open2')
    ret = FFmpeg.avcodec_parameters_from_context(codecpar, codec_cxt)
    FFmpeg.assert(ret, 'avcodec_parameters_from_context')
    FFmpeg.av_dump_format(fmt_ctx[0], 0, opt[0], 1);
    --open the output file, if needed
    if bit.band(fmt_ctx[0].oformat.flags, FFmpeg.AVFMT_NOFILE) == 0 then 
        if not _OPT.y and io.open(opt[0], 'r') then
            if _OPT.n then
                FFmpeg.av_log(nil, FFmpeg.AV_LOG_ERROR,
                "File '%s' already exists. Exiting.", opt[0])
                os.exit(0)
            else
                FFmpeg.av_log(nil, FFmpeg.AV_LOG_ERROR,
                "File '%s' already exists. Overwrite ? [y/N]", opt[0])
                if io.read('*l') ~= 'y' then os.exit(0) end
            end
        end
        local pb = ffi.new('AVIOContext*[1]')
        ret = FFmpeg.avio_open(pb, opt[0], FFmpeg.AVIO_FLAG_WRITE)
        assert(ret, opt[0])
        fmt_ctx[0].pb = pb[0]
    end
    return fmt_ctx, stream, codec_cxt
end
local function open_input(opt)
    local fmt_ctx = ffi.new('AVFormatContext*[1]')
    local ret = FFmpeg.avformat_open_input(fmt_ctx, opt[0], opt.f, nil)--TODO options
    FFmpeg.assert(ret, 'avformat_open_input')

    local ret = FFmpeg.avformat_find_stream_info(fmt_ctx[0], nil)
    FFmpeg.assert(ret, 'avformat_find_stream_info')
    --输出调试信息:
    FFmpeg.av_dump_format(fmt_ctx[0], 0, opt[0], 0);
    --[[print[[
    tbr代表帧率
    tbn代表文件层（st）的时间精度，即1S=1200k，和duration相关
    tbc代表视频层（st->codec）的时间精度，即1S=XX，和stream->duration和时间戳相关)
    ]]
    --寻找视频流，有麦克风的摄像头同时还有音频流输入
    local videoStreamIndex
    for i=0, fmt_ctx[0].nb_streams-1 do
        if fmt_ctx[0].streams[i].codec.codec_type == FFmpeg.AVMEDIA_TYPE_VIDEO then
            videoStreamIndex = i
            break
        end
    end
    assert(videoStreamIndex)

    local stream = fmt_ctx[0].streams[videoStreamIndex]
    local codecpar = stream.codecpar

    --print ('输入视频格式:', ffi.string(FFmpeg.avcodec_get_name(codecpar.codec_id)))
    --print ('输入视频高度:', codecpar.height)
    --print ('输入视频宽度:', codecpar.width)
    --获取帧率信息，注意输入流里面用r_frame_rate不用avg_frame_rate
    --print ('输入视频帧率:', stream.r_frame_rate.num * 1.0 /  stream.r_frame_rate.den)

    --创建解码的AVCodecContext
    local codec = FFmpeg.avcodec_find_decoder(codecpar.codec_id)
    local codec_cxt = FFmpeg.avcodec_alloc_context3(codec);
    local ret = FFmpeg.avcodec_parameters_to_context(codec_cxt, codecpar)
    FFmpeg.assert(ret, 'avcodec_parameters_to_context')
    --codec_cxt.pix_fmt = FFmpeg.AV_PIX_FMT_YUV420P
    --filter_codec_opts
    local dict = _OPT.codec_dict(opt, codecpar.codec_id, fmt_ctx[0], stream, codec)
    local ret = FFmpeg.avcodec_open2(codec_cxt, codec, dict)
    FFmpeg.assert(ret, 'avcodec_open2')

    return fmt_ctx, stream, codec_cxt
end
----------------------------------------------------------------------------------
local function process(arg)
    local i_ctx, i_stm, decoder= open_input(_INPUTS[1])
    local o_ctx, o_stm, encoder= open_output(i_stm, _OPT[1])
----------------------------------------------------------------------------------
local i_par, o_par = i_stm.codecpar,o_stm.codecpar
local s_ctx = FFmpeg.sws_getCachedContext(nil,
i_par.width, i_par.height, i_par.format,
o_par.width, o_par.height, o_par.format,
FFmpeg.SWS_BICUBIC, nil, nil, nil)
assert(s_ctx)
--------------------------------------------------------------------------------
--local options = ffi.new('AVDictionary*[1]')
--FFmpeg.av_dict_set(options, 'window_title', 'dshow-sdl', 0);
--FFmpeg.av_dict_set(options, 'window_borderless', '0', 0);
local ret = FFmpeg.avformat_write_header(o_ctx[0], nil)
assert(ret)
----------------------------------------------------------------------------------
local frame = FFmpeg.av_frame_alloc();
local packet = FFmpeg.av_packet_alloc();
local frame2 = FFmpeg.av_frame_alloc();
frame2.format = o_par.format;
frame2.width  = o_par.width;
frame2.height = o_par.height;
FFmpeg.av_frame_get_buffer(frame2, 32);
while not _TTY.sigterm do
    local cur_time= FFmpeg.av_gettime_relative()
    if _TTY.check(cur_time) < 0 then break end

    ret = FFmpeg.av_read_frame(i_ctx[0], packet)
    if ret < 0 then break end
    ret = FFmpeg.avcodec_send_packet(decoder, packet);
    while ret == 0 do
        ret = FFmpeg.avcodec_receive_frame(decoder, frame);
        if ret == 0 then
            FFmpeg.sws_scale(s_ctx, ffi.cast('const unsigned char *const*', frame.data), frame.linesize, 0, i_par.height, frame2.data, frame2.linesize)
            ret = FFmpeg.avcodec_send_frame(encoder, frame2);
        end
        while ret == 0 do
            ret = FFmpeg.avcodec_receive_packet(encoder, packet);
            if ret == 0 then 
                ret = FFmpeg.av_write_frame(o_ctx[0], packet);
                if ret ~= 0 then goto clean end
            end
        end
        FFmpeg.av_frame_unref(frame);
    end
    FFmpeg.av_packet_unref(packet)
end
----------------------------------------------------------------------------------
::clean::
local ret = FFmpeg.av_write_trailer(o_ctx[0])
assert(ret)
FFmpeg.avcodec_free_context(ffi.new('AVCodecContext*[1]', encoder))
FFmpeg.avcodec_free_context(ffi.new('AVCodecContext*[1]', decoder))
if o_ctx[0].pb then --close the output file
    FFmpeg.avio_closep(ffi.new('AVIOContext*[1]', o_ctx[0].pb))
    o_ctx[0].pb = nil
end
FFmpeg.av_frame_free(ffi.new('AVFrame*[1]', frame))
FFmpeg.av_frame_free(ffi.new('AVFrame*[1]', frame2))
FFmpeg.av_packet_free(ffi.new('AVPacket*[1]', packet))
FFmpeg.avformat_close_input(i_ct);
FFmpeg.avformat_close_input(o_ctx);
end
local scuess, reason = pcall(transcode)
assert(scuess, reason)
