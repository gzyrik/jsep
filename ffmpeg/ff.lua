local SEP = string.sub(package.config, 1, 1)
local function local_dir()
    local f=debug.getinfo(2, 'S').source:sub(2)
    if SEP == '\\' then
        local file = io.popen('dir /B /S '..f)
        f = file:read('*all')
        file:close()
    elseif string.sub(f,1,1) ~= SEP then
        f = os.getenv('PWD')..SEP..f
    end
    return string.match(f,'^(.*)ff%.lua')
end
local cwd = local_dir()
--------------------------------------------------------------------------------
local _OPT, ret = loadfile(cwd..'ff-opt.lua')
assert(_OPT, ret)
local ret, FFmpeg, _OPT, _URL = pcall(_OPT, cwd, arg)
assert(ret, FFmpeg)
--_OPT表: Hash部分为全局参数, Array部分为输出文件`FILE表'
--_URL表: Array部分为输入文件`FILE表'
--`FILE表': Hash部分为参数集合
--      [-2] 为有效命令参数集合, 使用 mark_used 标记为已使用
--      [-1] 为url
--      [0] 为该文件`运行态资源'{
--          fmtctx=AVFormatContext*[1],
--      }
--      [...]为该文件中流AVStream.index+1 对应的`编解码单元' {
--          avctx=AVCodecContext*[1],
--          frame=AVFrame*[1],
--          packet=AVPacket*[1],
--          //编码特有,对应解码器=_URL[uid][sid]
--          uid=<uint>, 输入源在_URL中的序号,[1,#_URL]
--          sid=<uint>, 输入源文件中的流序号,[1,fmtctx.nb_streams]
--          swsctx=SwsContext*,
--      }
local url, fmtctx = _OPT.url, _OPT.fmtctx
--------------------------------------------------------------------------------
local ffi, bit = require'ffi', require'bit'
local function tohex(i)
    if i==0 then return '0' end
    local s = bit.tohex(i,16)
    return string.match(s,'([^0].*)$')
end
local function config_filtergraph(filter, ist, decoder, name)
    -- create input filter
    local ipar = ist.codecpar
    local args = string.format(
    'video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d',
    ipar.width, ipar.height,
    tonumber(decoder.pix_fmt),
    ist.time_base.num, ist.time_base.den,
    ipar.sample_aspect_ratio.num, ipar.sample_aspect_ratio.den)
    local buffer_filt = FFmpeg.avfilter_get_by_name('buffer')
    local fltctx_in = ffi.new('AVFilterContext*[1]') -- free by graph 
    local ret = FFmpeg.avfilter_graph_create_filter(fltctx_in, buffer_filt, name.buffer, args, nil, filter.graph[0])
    FFmpeg.assert(ret, "Failed to create buffer source")
    ret = FFmpeg.avfilter_link(fltctx_in[0], 0, filter.inputs[0].filter_ctx, filter.inputs[0].pad_idx)
    FFmpeg.assert(ret, "Failed to link buffer source")
    -- create output filter
    local fltctx_out = ffi.new('AVFilterContext*[1]') -- free by graph
    local buffersink = FFmpeg.avfilter_get_by_name("buffersink")
    local ret = FFmpeg.avfilter_graph_create_filter(fltctx_out, buffersink, name.buffersink, nil, nil, filter.graph[0])
    FFmpeg.assert(ret, "Failed to create buffer sink");
    ret = FFmpeg.avfilter_link(filter.outputs[0].filter_ctx, filter.outputs[0].pad_idx, fltctx_out[0], 0)
    FFmpeg.assert(ret, "Failed to link buffer sink")

    ret = FFmpeg.avfilter_graph_config(filter.graph[0], nil)
    FFmpeg.assert(ret, 'avfilter_graph_config')
    local graph_desc = FFmpeg.avfilter_graph_dump(filter.graph[0], nil)
    graph_desc=ffi.gc(graph_desc, FFmpeg.av_free)
    --FFmpeg.av_log(fmtctx(ofile), FFmpeg.AV_LOG_INFO, graph_desc)
    return fltctx_in, fltctx_out
end

local function open_stream(ifile, uid, sid)
    local name, ctx = url(ifile),fmtctx(ifile)
    assert(sid <= ctx.nb_streams)
    local ist = ctx.streams[sid-1]
    assert(ist.index+1 == sid, name)
    if ifile[sid] then return ist, ifile[sid].avctx[0] end

    local ipar = ist.codecpar
    if ipar.channels > 0 and ipar.channel_layout == 0 then
        ipar.channel_layout = FFmpeg.av_get_default_channel_layout(ipar.channels)
    end
    local codec = FFmpeg.avcodec_find_decoder(ipar.codec_id)
    local avctx = ffi.new('AVCodecContext*[1]', FFmpeg.avcodec_alloc_context3(codec))
    avctx = ffi.gc(avctx, FFmpeg.avcodec_free_context)

    local ret = FFmpeg.avcodec_parameters_to_context(avctx[0], ipar)
    FFmpeg.assert(ret, name)

    local dict = _OPT.codec_dict(ifile, ipar.codec_id, fmtctx(ifile), ist, codec)
    local ret = FFmpeg.avcodec_open2(avctx[0], codec, dict)
    FFmpeg.assert(ret, name)
    local packet = ffi.new('AVPacket*[1]', FFmpeg.av_packet_alloc())
    packet = ffi.gc(packet, FFmpeg.av_packet_free)

    local frame = ffi.new('AVFrame*[1]', FFmpeg.av_frame_alloc())
    frame = ffi.gc(frame, FFmpeg.av_frame_free)
    if ipar.codec_type == FFmpeg.AVMEDIA_TYPE_VIDEO then frame[0].nb_samples = 1 end

    ifile[sid] = {
        avctx = avctx,
        frame = frame,
        packet = packet,
        frame_time = 0,
    }
    return ist, avctx[0]
end
local function add_stream(ofile, uid, sid, info)
    local ctx, ifile = fmtctx(ofile), _URL[uid]
    local ist, decoder = open_stream(ifile, uid, sid)
    local ipar = ist.codecpar
    local ost = FFmpeg.avformat_new_stream(ctx, nil)
    local opar = ost.codecpar

    --fill default parameter
    ost.time_base, ost.sample_aspect_ratio = ist.time_base, ipar.sample_aspect_ratio
    opar.codec_type, opar.codec_id, opar.format = ipar.codec_type, ipar.codec_id, ipar.format
    opar.sample_aspect_ratio, opar.frame_size = ipar.sample_aspect_ratio, ipar.frame_size
    opar.width, opar.height = ipar.width, ipar.height
    opar.channels, opar.sample_rate, opar.channel_layout = ipar.channels, ipar.sample_rate, ipar.channel_layout

    if ctx.oformat ~= nil then
        if opar.codec_type == FFmpeg.AVMEDIA_TYPE_VIDEO then
            opar.codec_id = ctx.oformat.video_codec
        elseif opar.codec_type == FFmpeg.AVMEDIA_TYPE_AUDIO then
            opar.codec_id = ctx.oformat.audio_codec
        elseif opar.codec_type == FFmpeg.AVMEDIA_TYPE_SUBTITLE then
            opar.codec_id = ctx.oformat.subtitle_codec
        end
    end
    local ifmt, iw, ih, ilayout, irate = opar.format, opar.width, opar.height, opar.channel_layout, opar.sample_rate
    --add filter
    local fltctx_in, fltctx_out, flt_frame
    if info.filter then
        fltctx_in, fltctx_out = config_filtergraph(info.filter, ist, decoder, {
            buffer=string.format("%d:%d",uid-1,sid-1),
            buffersink=string.format("%s:%d", url(ofile), ost.index)
        })
        --update parameter
        opar.format = FFmpeg.av_buffersink_get_format(fltctx_out[0])
        opar.width = FFmpeg.av_buffersink_get_w(fltctx_out[0])
        opar.height = FFmpeg.av_buffersink_get_h(fltctx_out[0])
        ifmt, iw, ih = opar.format, opar.width, opar.height

        flt_frame = ffi.new('AVFrame*[1]', FFmpeg.av_frame_alloc())
        flt_frame = ffi.gc(flt_frame, FFmpeg.av_frame_free)

        flt_frame[0].nb_samples = 1
        flt_frame[0].format = opar.format
        flt_frame[0].width  = opar.width
        flt_frame[0].height = opar.height
        ret = FFmpeg.av_image_alloc(flt_frame[0].data, flt_frame[0].linesize, opar.width, opar.height, opar.format, 4)
        FFmpeg.assert(ret, 'av_image_alloc')
        flt_frame[0].extended_data = flt_frame[0].data -- extended_data will be freed by av_frame_free
    end

    local codec
    if opar.codec_type == FFmpeg.AVMEDIA_TYPE_VIDEO then -- override video parameter
        local vcodec = _OPT.specifier(ofile.c, ctx, ost)
        if vcodec then
            _OPT.mark_used(ofile, 'c', vcodec)
            opar.codec_id = vcodec
        end
        if ofile.pix_fmt then
            opar.format = ofile.pix_fmt
            _OPT.mark_used(ofile, 'pix_fmt')
        end

        codec = FFmpeg.avcodec_find_encoder(opar.codec_id)
        if codec.pix_fmts ~= nil then
            opar.format = _OPT.choose_pix_fmt(codec, opar.format)
        end
        local vsize = _OPT.specifier(ofile.s, ctx, ost)
        if vsize then
            _OPT.mark_used(ofile, 's', vsize)
            local w, h = string.match(vsize, '^(%d*)x(%d*)$')
            assert(w and h, vsize)
            opar.width, opar.height = tonumber(w), tonumber(h)
        end

        if ofile.framerate then
            opar.sample_rate = tonumber(ofile.framerate)
            _OPT.mark_used(ofile, 'framerate')
        elseif fltctx_out then
            local frame_rate = FFmpeg.av_buffersink_get_frame_rate(fltctx_out[0])
            if frame_rate.num > 0 then opar.sample_rate = frame_rate.num/frame_rate.den end
        end
        if opar.sample_rate == 0 then opar.sample_rate = 25 end-- default 25 fps

    elseif opar.codec_type == FFmpeg.AVMEDIA_TYPE_AUDIO then -- override audio parameter

        codec = FFmpeg.avcodec_find_encoder(opar.codec_id)
        if codec.sample_fmts ~= nil then
            opar.format = _OPT.choose_sample_fmt(codec, opar.format)
        end

        if fltctx_out then
            opar.format         = FFmpeg.av_buffersink_get_format(fltctx_out[0])
            opar.sample_rate    = FFmpeg.av_buffersink_get_sample_rate(fltctx_out[0])
            opar.channels       = FFmpeg.av_buffersink_get_channels(fltctx_out[0])
            opar.channel_layout = FFmpeg.av_buffersink_get_channel_layout(fltctx_out[0])
        end
    end
   
     --创建编码的AVCodecContext
    local avctx = ffi.new('AVCodecContext*[1]', FFmpeg.avcodec_alloc_context3(codec))
    avctx = ffi.gc(avctx, FFmpeg.avcodec_free_context)
    avctx[0].time_base.num, avctx[0].time_base.den = 1, opar.sample_rate

    local ret = FFmpeg.avcodec_parameters_to_context(avctx[0], opar)
    FFmpeg.assert(ret, 'avcodec_parameters_to_context')
    local dict = _OPT.codec_dict(ofile, opar.codec_id, ctx, ost, codec)
    local ret = FFmpeg.avcodec_open2(avctx[0], codec, dict)
    FFmpeg.assert(ret, 'avcodec_open2')
    ret = FFmpeg.avcodec_parameters_from_context(opar, avctx[0])
    FFmpeg.assert(ret, 'avcodec_parameters_from_context')

    local packet = ffi.new('AVPacket*[1]', FFmpeg.av_packet_alloc())
    packet = ffi.gc(packet, FFmpeg.av_packet_free)
    packet[0].stream_index = ist.index

    -- prepare convert
    local swsctx, sws_frame, swrctx, swr_frame, audio_fifo, fifo_frame
    if opar.codec_type == FFmpeg.AVMEDIA_TYPE_VIDEO then
        if ifmt ~= opar.format or iw ~= opar.width or ih ~= opar.height then
            swsctx = FFmpeg.sws_getCachedContext(nil, iw, ih,ifmt,
            opar.width, opar.height, opar.format,
            FFmpeg.SWS_BICUBIC, nil, nil, nil)
            swsctx = ffi.gc(swsctx, sws_freeContext)
            FFmpeg.av_log(ctx, FFmpeg.AV_LOG_WARNING, 'swsctx %s:%s->%s:%s\n',
            FFmpeg.av_get_pix_fmt_name(ifmt), iw..'x'..ih,
            FFmpeg.av_get_pix_fmt_name(opar.format), opar.width..'x'..opar.height)

            sws_frame = ffi.new('AVFrame*[1]', FFmpeg.av_frame_alloc())
            sws_frame = ffi.gc(sws_frame, FFmpeg.av_frame_free)

            sws_frame[0].nb_samples = 1
            sws_frame[0].format = opar.format
            sws_frame[0].width  = opar.width
            sws_frame[0].height = opar.height
            ret=FFmpeg.av_image_alloc(sws_frame[0].data, sws_frame[0].linesize, opar.width, opar.height, opar.format, 4)
            FFmpeg.assert(ret, 'av_image_alloc')
            sws_frame[0].extended_data = sws_frame[0].data -- extended_data will be freed by av_frame_free
        end
    elseif opar.codec_type == FFmpeg.AVMEDIA_TYPE_AUDIO then
        if ilayout ~= opar.channel_layout or ifmt ~= opar.format or irate ~= opar.sample_rate then
            swrctx = ffi.new('SwrContext*[1]', FFmpeg.swr_alloc())
            swrctx = ffi.gc(swrctx, FFmpeg.swr_alloc)
            FFmpeg.av_log(ctx, FFmpeg.AV_LOG_WARNING, 'swrctx %s:%s->%s:%s\n',
            FFmpeg.av_get_sample_fmt_name(ifmt), irate..' Hz, 0x'..tohex(ilayout)..' Ch',
            FFmpeg.av_get_sample_fmt_name(opar.format), opar.sample_rate..' Hz, 0x'..tohex(opar.channel_layout)..' Ch')

            swr_frame = ffi.new('AVFrame*[1]', FFmpeg.av_frame_alloc())
            swr_frame = ffi.gc(swr_frame, FFmpeg.av_frame_free)

            swr_frame[0].channels = opar.channels
            swr_frame[0].format = opar.format
            swr_frame[0].nb_samples = opar.frame_size 
            swr_frame[0].channel_layout = opar.channel_layout
            swr_frame[0].sample_rate = opar.sample_rate
        end
        fifo_frame = swr_frame
        if not fifo_frame then
            fifo_frame = ffi.new('AVFrame*[1]', FFmpeg.av_frame_alloc())
            fifo_frame = ffi.gc(fifo_frame, FFmpeg.av_frame_free)
            assert(opar.frame_size > 0)

            fifo_frame[0].channels = opar.channels
            fifo_frame[0].format = opar.format
            fifo_frame[0].nb_samples = opar.frame_size 
            fifo_frame[0].channel_layout = opar.channel_layout
            fifo_frame[0].sample_rate = opar.sample_rate
            ret = FFmpeg.av_frame_get_buffer(fifo_frame[0], 4)
            FFmpeg.assert(ret, 'av_frame_get_buffer')
        end
        audio_fifo = FFmpeg.av_audio_fifo_alloc(opar.format, opar.channels, opar.frame_size)
        audio_fifo = ffi.gc(audio_fifo, FFmpeg.av_audio_fifo_free) 
    end

    ofile[ost.index+1] = {
        avctx = avctx,
        packet = packet,
        uid=uid,
        sid=sid,
        pts=0,

        -- filter
        fltctx_in = fltctx_in,
        fltctx_out = fltctx_out,
        flt_frame = flt_frame,
        filter_info = info.filter,

        -- video
        swsctx = swsctx, sws_frame = sws_frame,
        
        -- audio
        swrctx = swrctx, swr_frame = swr_frame,
        audio_fifo=audio_fifo, fifo_frame = fifo_frame,
    }
    -- change other parameter
    if ofile.f == 'sdl' and ifile.re == nil then ifile.re = true end
    _OPT.mark_used(ifile, 're')
end
local function open_ofile(ofile)
    local name, ctx, ret = url(ofile), fmtctx(ofile, true)
    local media = _OPT.stream_map(ofile)
    for uid, st in pairs(media) do
        for sid, info in pairs(st) do
            add_stream(ofile, uid, sid, info)
        end
    end
    FFmpeg.av_dump_format(ctx, 0, name, 1)

    --open the output url, if needed
    if bit.band(ctx.oformat.flags, FFmpeg.AVFMT_NOFILE) == 0 then 
        _OPT.confirm_file(name)
        local pb = ffi.new('AVIOContext*[1]')
        ret = FFmpeg.avio_open(pb, name, FFmpeg.AVIO_FLAG_WRITE)
        FFmpeg.assert(ret, name)
        ctx.pb = pb[0]
    end
    ret = FFmpeg.avformat_write_header(ctx, nil)
    FFmpeg.assert(ret, name)
    _OPT.check_arg(ofile)
end
local function choose_ocell(ofile)
    for i, cell in ipairs(ofile) do
        if cell.avctx then return cell, i-1 end
    end
end
local function close_ocell(ocell)
    if not ocell.avctx then return end
    ocell.avctx, ocell.audio_fifo = nil
    ocell.filter_info = nil
    ocell.flt_frame = nil
    ocell.sws_frame = nil
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
    ofile[0] = nil
end
local function close_ifile(ifile)
    if not ifile[0] then return end
    for _, icell in ipairs(ifile) do
        icell.avctx, icell.frame, icell.packet = nil
    end
    ifile[0] = nil
end
--receive frame frome the peer decoder
local function receive_frame(ocell, cur_time)
    local ifile = _URL[ocell.uid]
    local ctx, icell = fmtctx(ifile), ifile[ocell.sid]
    local ret, ist = 0, ctx.streams[ocell.sid-1]
    local decoder, frame, packet = icell.avctx[0], icell.frame[0], icell.packet[0]
    if icell.frame_time == cur_time then goto finish end
    if ifile.re == true and icell.frame_time > cur_time then
        return FFmpeg.AVERROR_AGAIN, icell.frame_time
    end

    ::retry::
    ret = FFmpeg.av_read_frame(ctx, packet)
    if ret ~= 0 then return ret end
    if packet.stream_index + 1 ~= ocell.sid then goto retry end

    ret = FFmpeg.avcodec_send_packet(decoder, packet)
    if ret ~= 0 then return ret end

    ret = FFmpeg.avcodec_receive_frame(decoder, frame)
    if ret == FFmpeg.AVERROR_AGAIN then goto retry
    elseif ret == 0 then
        if not icell.first_effort_timestamp then icell.first_effort_timestamp = frame.best_effort_timestamp end
        frame.best_effort_timestamp = frame.best_effort_timestamp - icell.first_effort_timestamp
        if ifile.re == true then
            icell.frame_time = frame.best_effort_timestamp * 1000000 * ist.time_base.num / ist.time_base.den
            if icell.frame_time > cur_time then
                return FFmpeg.AVERROR_AGAIN, icell.frame_time
            end
        else
            icell.frame_time = cur_time
        end
    end

    ::finish::
    return ret, frame, ist
end
local function fifo_read_frame(ocell)
    local frame = ocell.fifo_frame[0]
    frame.nb_samples = FFmpeg.av_audio_fifo_size(ocell.audio_fifo)
    if frame.nb_samples == 0 then
        return
    elseif frame.nb_samples > ocell.avctx[0].frame_size then
        frame.nb_samples = ocell.avctx[0].frame_size
    end
    local ret = FFmpeg.av_audio_fifo_read(ocell.audio_fifo, ffi.cast('void**', frame.data), frame.nb_samples)
    FFmpeg.assert(ret, 'av_audio_fifo_read')
    return frame, ocell.pts
end
local function transcode_step(ofile, cur_time)
    local ocell, stream_index = choose_ocell(ofile)
    if not ocell then return close_ofile(ofile) end
    local ctx, encoder, packet = fmtctx(ofile), ocell.avctx[0], ocell.packet[0]
    local ost, seq = ctx.streams[stream_index]

    local ret, frame, ist = receive_frame(ocell, cur_time)
    if ret == FFmpeg.AVERROR_EOF then
        if ocell.audio_fifo then frame, seq = fifo_read_frame(ocell) end
        goto flush
    elseif ret == FFmpeg.AVERROR_AGAIN then
        return frame
    end
    FFmpeg.assert(ret, 'receive_frame')

    -- encoder sequence number
    seq = FFmpeg.av_rescale_q(frame.best_effort_timestamp, ist.time_base, encoder.time_base)
    if seq < ocell.pts then return end --ignore the old frame

    if ocell.fltctx_in then
        ret = FFmpeg.av_buffersrc_add_frame_flags(ocell.fltctx_in[0], frame, FFmpeg.AV_BUFFERSRC_FLAG_KEEP_REF)
        FFmpeg.assert(ret, 'Error while feeding the filtergraph')
    end
    if ocell.fltctx_out then
        ret = FFmpeg.av_buffersink_get_frame(ocell.fltctx_out[0], ocell.flt_frame[0])
        if ret == FFmpeg.AVERROR_AGAIN then return end
        FFmpeg.assert(ret, 'Error while fetching the filtergraph')
        frame = ocell.flt_frame[0]
    end
    if ocell.swsctx ~= nil then
        ret = FFmpeg.sws_scale(ocell.swsctx, ffi.cast('const unsigned char *const*', frame.data),
        frame.linesize, 0, ocell.sws_frame[0].height, ocell.sws_frame[0].data, ocell.sws_frame[0].linesize)
        FFmpeg.assert(ret, 'sws_scale')
        frame = ocell.sws_frame[0]
    end
    if ocell.swrctx ~= nil then
        ret = FFmpeg.swr_convert_frame(ocell.swrctx[0], ocell.swr_frame[0], frame)
        FFmpeg.assert(ret, 'swr_convert_frame')
        frame = ocell.swr_frame[0]
    end
    if ocell.audio_fifo and (frame.nb_samples ~= encoder.frame_size or FFmpeg.av_audio_fifo_size(ocell.audio_fifo) > 0) then
        ret = FFmpeg.av_audio_fifo_write(ocell.audio_fifo, ffi.cast('void**', frame.data), frame.nb_samples)
        FFmpeg.assert(ret, 'av_audio_fifo_write')
        if FFmpeg.av_audio_fifo_size(ocell.audio_fifo) < encoder.frame_size then return end
        frame, seq = fifo_read_frame(ocell)
    end
    ::flush::
    if frame then frame.pts, ocell.pts = seq, seq+frame.nb_samples end
    ret = FFmpeg.avcodec_send_frame(encoder, frame)
    FFmpeg.assert(ret, 'avcodec_send_frame')
    while true do
        assert(packet.size == 0 and packet.stream_index == stream_index)
        ret = FFmpeg.avcodec_receive_packet(encoder, packet)
        if ret == FFmpeg.AVERROR_AGAIN or ret == FFmpeg.AVERROR_EOF then break end
        FFmpeg.assert(ret, 'avcodec_receive_packet')
        packet.pts = FFmpeg.av_rescale_q(packet.pts, encoder.time_base, ost.time_base)
        packet.dts = FFmpeg.av_rescale_q(packet.dts, encoder.time_base, ost.time_base)
        ret = FFmpeg.av_interleaved_write_frame(ctx, packet)
        if ret == FFmpeg.AVERROR_EIO then return close_ofile(ofile) end
        FFmpeg.assert(ret, 'av_interleaved_write_frame')
    end
    if frame == nil then
        close_ocell(ocell)
    elseif ocell.audio_fifo and FFmpeg.av_audio_fifo_size(ocell.audio_fifo) >= encoder.frame_size then
        frame, seq = fifo_read_frame(ocell)
        goto flush
    end
end
--------------------------------------------------------------------------------
local _TTY, ret = loadfile(cwd..'ff-tty.lua')
assert(_TTY, ret)
ret, _TTY = pcall(_TTY, FFmpeg, _OPT)
assert(ret, _TTY)
--------------------------------------------------------------------------------
local function transcode()
    for _, ofile in ipairs(_OPT) do open_ofile(ofile) end
    for _, ifile in ipairs(_URL) do _OPT.check_arg(ifile) end
    _OPT.print_sdp()
    local start_point, cur_time = FFmpeg.av_gettime_relative(), 0 -- microseconds
    while not _TTY.sigterm do
        if _TTY.check(cur_time) < 0 then break end
        local seek_time
        for _, ofile in ipairs(_OPT) do
            if ofile[0] then
                local next_time = transcode_step(ofile, cur_time) or cur_time
                if not seek_time or next_time < seek_time then seek_time = next_time end
            end
        end
        if not seek_time then break end
        if seek_time > cur_time then
            cur_time = seek_time
            seek_time = seek_time + start_point - FFmpeg.av_gettime_relative()
            if seek_time > 0 then FFmpeg.av_usleep(seek_time) end
        else
            cur_time = FFmpeg.av_gettime_relative() - start_point
        end
    end
    for _, ifile in ipairs(_URL) do close_ifile(ifile) end
    for _, ofile in ipairs(_OPT) do close_ofile(ofile) end
end
--------------------------------------------------------------------------------
local scuess, reason = pcall(transcode)
assert(scuess, reason)
