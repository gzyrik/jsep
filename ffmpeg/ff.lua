local SEP = string.sub(package.config, 1, 1)
local function local_dir()
    local f=debug.getinfo(2, 'S').source:sub(2)
    if SEP == '\\' then
        local file = io.popen('dir /B /S '..f)
        f = file:read('*all')
        file:close()
    else
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
local function open_stream(uid, sid, info)
    local ifile = _URL[uid]
    local name, ctx = url(ifile),fmtctx(ifile)
    assert(sid <= ctx.nb_streams)
    local ist = ctx.streams[sid-1]
    assert(ist.index+1 == sid, name)
    if ifile[sid] then return ist end

    local ipar = ist.codecpar
    local codec = FFmpeg.avcodec_find_decoder(ipar.codec_id)
    local avctx = ffi.new('AVCodecContext*[1]', FFmpeg.avcodec_alloc_context3(codec))
    avctx = ffi.gc(avctx, FFmpeg.avcodec_free_context)

    local ret = FFmpeg.avcodec_parameters_to_context(avctx[0], ipar)
    FFmpeg.assert(ret, name)

    local dict = _OPT.codec_dict(ifile, ipar.codec_id, fmtctx(ifile), ist, codec)
    local ret = FFmpeg.avcodec_open2(avctx[0], codec, dict)
    FFmpeg.assert(ret, name)
    local fltctx
    if info.filter then
        local name = string.format("%d:%d",uid-1,sid-1)
        local args = string.format(
        'video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d',
        ipar.width,
        ipar.height,
        tonumber(avctx[0].pix_fmt),
        ist.time_base.num,
        ist.time_base.den,
        ipar.sample_aspect_ratio.num,
        ipar.sample_aspect_ratio.den)
        local buffer_filt = FFmpeg.avfilter_get_by_name('buffer')
        fltctx = ffi.new('AVFilterContext*[1]') -- free by graph 
        ret = FFmpeg.avfilter_graph_create_filter(fltctx, buffer_filt, name, args, nil, info.filter.graph[0])
        FFmpeg.assert(ret, "Failed to create buffer source")
        ret = FFmpeg.avfilter_link(fltctx[0], 0, info.filter.inputs[0].filter_ctx, info.filter.inputs[0].pad_idx)
        FFmpeg.assert(ret, "Failed to link buffer source")
    end
    local packet = ffi.new('AVPacket*[1]', FFmpeg.av_packet_alloc())
    packet = ffi.gc(packet, FFmpeg.av_packet_free)

    local frame = ffi.new('AVFrame*[1]', FFmpeg.av_frame_alloc())
    frame = ffi.gc(frame, FFmpeg.av_frame_free)

    ifile[sid] = {
        avctx = avctx,
        fltctx = fltctx,
        frame = frame,
        packet = packet,
    }
    return ist
end
local function add_stream(ofile, uid, sid, info)
    local ctx = fmtctx(ofile)
    local ist = open_stream(uid, sid, info)
    local ipar = ist.codecpar
    local ost = FFmpeg.avformat_new_stream(ctx, nil)
    local opar = ost.codecpar
    --fill default parameter
    ost.time_base, ost.sample_aspect_ratio = ist.time_base, ipar.sample_aspect_ratio
    opar.codec_type, opar.codec_id, opar.format = ipar.codec_type, ipar.codec_id, ipar.format
    opar.sample_aspect_ratio = ipar.sample_aspect_ratio
    opar.width, opar.height = ipar.width, ipar.height
    if ctx.oformat ~= nil then
        if opar.codec_type == FFmpeg.AVMEDIA_TYPE_VIDEO then
            opar.codec_id = ctx.oformat.video_codec
        elseif opar.codec_type == FFmpeg.AVMEDIA_TYPE_AUDIO then
            opar.codec_id = ctx.oformat.audio_codec
        elseif opar.codec_type == FFmpeg.AVMEDIA_TYPE_SUBTITLE then
            opar.codec_id = ctx.oformat.subtitle_codec
        end
    end
    local ifmt, iw, ih = opar.format, opar.width, opar.height
    --add filter
    local fltctx,flt_frame
    if info.filter then
        fltctx = ffi.new('AVFilterContext*[1]') -- free by graph
        local buffersink = FFmpeg.avfilter_get_by_name("buffersink")
        local name = string.format("%s:%d", url(ofile), ost.index)
        ret = FFmpeg.avfilter_graph_create_filter(fltctx, buffersink, name, nil, nil, info.filter.graph[0])
        FFmpeg.assert(ret, "Failed to create buffer sink");
        ret = FFmpeg.avfilter_link(info.filter.outputs[0].filter_ctx, info.filter.outputs[0].pad_idx, fltctx[0], 0)
        FFmpeg.assert(ret, "Failed to link buffer sink")

        ret = FFmpeg.avfilter_graph_config(info.filter.graph[0], nil)
        FFmpeg.assert(ret, 'avfilter_graph_config')
        local graph_desc = FFmpeg.avfilter_graph_dump(info.filter.graph[0], nil)
        graph_desc=ffi.gc(graph_desc, FFmpeg.av_free)
        --FFmpeg.av_log(fmtctx(ofile), FFmpeg.AV_LOG_INFO, graph_desc)

        --update parameter
        opar.format = FFmpeg.av_buffersink_get_format(fltctx[0])
        opar.width = FFmpeg.av_buffersink_get_w(fltctx[0])
        opar.height = FFmpeg.av_buffersink_get_h(fltctx[0])
        flt_frame = ffi.new('AVFrame*[1]', FFmpeg.av_frame_alloc())
        flt_frame[0].format = opar.format
        flt_frame[0].width  = opar.width
        flt_frame[0].height = opar.height
        ret = FFmpeg.av_image_alloc(flt_frame[0].data, flt_frame[0].linesize, opar.width, opar.height, opar.format, 4)
        FFmpeg.assert(ret, 'av_image_alloc')
        local p = flt_frame[0].data[0]
        flt_frame=ffi.gc(flt_frame, function(f) FFmpeg.av_free(p);FFmpeg.av_frame_free(f) end)
        ifmt, iw, ih = opar.format, opar.width, opar.height
    end
    -- override parameter
    local vcodec = _OPT.specifier(ofile.c, ctx, ost)
    if vcodec then
        _OPT.mark_used(ofile, 'c', vcodec)
        opar.codec_id = vcodec
    end
    local codec = FFmpeg.avcodec_find_encoder(opar.codec_id)
    if ofile.pix_fmt then
        opar.format = ofile.pix_fmt
        _OPT.mark_used(ofile, 'pix_fmt')
    end
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
    local swsctx, sws_frame
    if ifmt ~= opar.format or iw ~= opar.width or ih ~= opar.height then
        swsctx = FFmpeg.sws_getCachedContext(nil, iw, ih,ifmt,
        opar.width, opar.height, opar.format,
        FFmpeg.SWS_BICUBIC, nil, nil, nil)
        FFmpeg.av_log(ctx, FFmpeg.AV_LOG_WARNING, string.format('swsctx %s:%dx%d->%s:%dx%d\n',
        ffi.string(FFmpeg.av_get_pix_fmt_name(ifmt)), iw, ih,
        ffi.string(FFmpeg.av_get_pix_fmt_name(opar.format)), opar.width, opar.height))
        sws_frame = ffi.new('AVFrame*[1]', FFmpeg.av_frame_alloc())
        sws_frame[0].format = opar.format
        sws_frame[0].width  = opar.width
        sws_frame[0].height = opar.height
        ret=FFmpeg.av_image_alloc(sws_frame[0].data, sws_frame[0].linesize, opar.width, opar.height, opar.format, 4)
        FFmpeg.assert(ret, 'av_image_alloc')
        local p = sws_frame[0].data[0]
        sws_frame=ffi.gc(sws_frame, function(f) Fmpeg.av_free(p); Fmpeg.av_frame_free(f) end)
    end
    --创建编码的AVCodecContext
    local avctx = ffi.new('AVCodecContext*[1]', FFmpeg.avcodec_alloc_context3(codec))
    avctx = ffi.gc(avctx, FFmpeg.avcodec_free_context)

    avctx[0].time_base.num, avctx[0].time_base.den = 1,25
    if ofile.framerate then
        avctx[0].time_base.num = tonumber(ofile.framerate)
        _OPT.mark_used(ofile, 'framerate')
    elseif fltctx then
        local fps = FFmpeg.av_buffersink_get_frame_rate(fltctx[0]).num
        if fps > 0 then avctx[0].time_base.num = fps end
    end

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

    ofile[ost.index+1] = {
        avctx = avctx,
        fltctx = fltctx,
        flt_frame = flt_frame,
        swsctx = swsctx,
        sws_frame = sws_frame,
        filter = info.filter,
        packet = packet,
        uid=uid,
        sid=sid,
        pts=0,
    }
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
local function choose_output_file()
    for _, ofile in ipairs(_OPT) do
        if ofile[0] then return ofile end
    end
end
local function choose_ocell(ofile)
    for i, cell in ipairs(ofile) do
        if cell.avctx then return cell, i-1 end
    end
end
local function close_ocell(ocell)
    if not ocell.avctx then return end
    ocell.avctx = nil
    ocell.filter = nil
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
        icell.avctx = nil
        icell.frame = nil
        icell.packet = nil
    end
    ifile[0] = nil
end
--receive frame frome the peer decoder
local function receive_frame(ocell, cur_time)
    local ifile = _URL[ocell.uid]
    local ctx, icell = fmtctx(ifile), ifile[ocell.sid]
    local ret, ist = 0, ctx.streams[ocell.sid-1]
    local decoder, frame, packet = icell.avctx[0], icell.frame[0], icell.packet[0]
    if frame.pts ~= FFmpeg.AV_NOPTS_VALUE then
        if frame.pts <= cur_time then
            frame.pts = FFmpeg.AV_NOPTS_VALUE
            return 0, frame, ist
        else
            return FFmpeg.AVERROR_AGAIN
        end
     end
    ::retry::
    ret = FFmpeg.av_read_frame(ctx, packet)
    if ret == 0 then
        if packet.stream_index + 1 ~= ocell.sid then goto retry end
        ret = FFmpeg.avcodec_send_packet(decoder, packet)
        if ret == 0 then
            ret = FFmpeg.avcodec_receive_frame(decoder, frame)
            if ret == FFmpeg.AVERROR_AGAIN then goto retry
            elseif ret == 0 then
                if icell.fltctx then
                    ret = FFmpeg.av_buffersrc_add_frame_flags(icell.fltctx[0], frame, FFmpeg.AV_BUFFERSRC_FLAG_KEEP_REF)
                    FFmpeg.assert(ret, 'Error while feeding the filtergraph')
                end
                if ifile.re then
                    local pos = frame.best_effort_timestamp * 1000000 * ist.time_base.num / ist.time_base.den
                    if pos > cur_time then
                        frame.pts = pos
                        return FFmpeg.AVERROR_AGAIN
                    end
                end
            end
        end
    end
    return ret, frame, ist
end
local function transcode_step(ofile, cur_time)
    local ocell, stream_index = choose_ocell(ofile)
    if not ocell then return close_ofile(ofile) end
    local ctx, encoder, packet = fmtctx(ofile), ocell.avctx[0], ocell.packet[0]
    local ost = ctx.streams[stream_index]

    local ret, frame, ist = receive_frame(ocell, cur_time)
    if ret == FFmpeg.AVERROR_EOF then return close_ocell(ocell)
    elseif ret == FFmpeg.AVERROR_AGAIN then return end
    FFmpeg.assert(ret, 'receive_frame')

    -- encoder sequence number
    local seq = FFmpeg.av_rescale_q(frame.best_effort_timestamp, ist.time_base, encoder.time_base)
    if seq < ocell.pts then return end

    if ocell.fltctx ~= nil then
        ret = FFmpeg.av_buffersink_get_frame(ocell.fltctx[0], ocell.flt_frame[0])
        if ret == FFmpeg.AVERROR_AGAIN then return end
        FFmpeg.assert(ret, 'Error while fetching the filtergraph')
        frame = ocell.flt_frame[0]
    end
    if ocell.swsctx ~= nil then
        ret = FFmpeg.sws_scale(ocell.swsctx, ffi.cast('const unsigned char *const*', frame.data),
        frame.linesize, 0, ocell.sws_frame[0].height, ocell.sws_frame[0].data, ocell.sws_frame[0].linesize)
        FFmpeg.assert(ret, 'Error sws_scale')
        frame = ocell.sws_frame[0]
    end
    frame.pts, ocell.pts = seq, seq+1
    ret = FFmpeg.avcodec_send_frame(encoder, frame)
    FFmpeg.assert(ret, 'avcodec_send_frame')
    while true do
        assert(packet.size == 0 and packet.stream_index == stream_index)
        ret = FFmpeg.avcodec_receive_packet(encoder, packet)
        if ret == FFmpeg.AVERROR_AGAIN then return end
        FFmpeg.assert(ret, 'avcodec_receive_packet')
        packet.pts = FFmpeg.av_rescale_q(packet.pts, encoder.time_base, ost.time_base)
        ret = FFmpeg.av_interleaved_write_frame(ctx, packet)
        if ret == FFmpeg.AVERROR_EIO then return close_ofile(ofile) end
        FFmpeg.assert(ret, 'av_interleaved_write_frame')
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
    local start_time = FFmpeg.av_gettime_relative() -- microseconds
    while not _TTY.sigterm do
        local cur_time= FFmpeg.av_gettime_relative()
        if _TTY.check(cur_time) < 0 then break end

        local ofile = choose_output_file()
        if not ofile then break end

        transcode_step(ofile, cur_time - start_time)
    end
    for _, ifile in ipairs(_URL) do close_ifile(ifile) end
    for _, ofile in ipairs(_OPT) do close_ofile(ofile) end
end
--------------------------------------------------------------------------------
local scuess, reason = pcall(transcode)
assert(scuess, reason)
