--------------------------------------------------------------------------------
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
--------------------------------------------------------------------------------
local function process(arg)
    local i_ctx, i_stm, decoder= open_input(_INPUTS[1])
    local o_ctx, o_stm, encoder= open_output(i_stm, _OPT[1])
--------------------------------------------------------------------------------
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
--------------------------------------------------------------------------------
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
--------------------------------------------------------------------------------
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
