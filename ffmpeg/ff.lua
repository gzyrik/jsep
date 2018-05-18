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
local function open_stream(uid, sid, info)
    local ifile = _URL[uid]
    local name = url(ifile)
    local ist = fmtctx(ifile).streams[sid-1]
    assert(ist.index+1 == sid, name)
    if ifile[sid] then return ist end

    local ipar = ist.codecpar
    local codec = FFmpeg.avcodec_find_decoder(ipar.codec_id)
    local avctx = ffi.new('AVCodecContext*[1]', FFmpeg.avcodec_alloc_context3(codec))
    ffi.gc(avctx, FFmpeg.avcodec_free_context)
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
    ffi.gc(packet, FFmpeg.av_packet_free)
    local frame = ffi.new('AVFrame*[1]', FFmpeg.av_frame_alloc())
    ffi.gc(frame, FFmpeg.av_frame_free)
    ifile[sid] = {
        avctx = avctx,
        fltctx = fltctx,
        frame = frame,
        packet = packet,
    }
    return ist
end
local function add_stream(ofile, uid, sid, info)
    local ist = open_stream(uid, sid, info)
    local ipar = ist.codecpar
    local ost = FFmpeg.avformat_new_stream(fmtctx(ofile), nil)
    local opar = ost.codecpar
    --fill default parameter
    ost.time_base.num, ost.time_base.den = ist.time_base.num, ist.time_base.den
    opar.codec_type = ipar.codec_type
    opar.format = ipar.format
    opar.codec_id = ipar.codec_id
    opar.width, opar.height = ipar.width, ipar.height
    local ifmt, iw, ih = opar.format, opar.width, opar.height
    
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
        ffi.gc(graph_desc, FFmpeg.av_free)
        --FFmpeg.av_log(fmtctx(ofile), FFmpeg.AV_LOG_INFO, graph_desc)

        --update parameter
        opar.format = FFmpeg.av_buffersink_get_format(fltctx[0])
        opar.width = FFmpeg.av_buffersink_get_w(fltctx[0])
        opar.height = FFmpeg.av_buffersink_get_h(fltctx[0])
        flt_frame = ffi.new('AVFrame*[1]', FFmpeg.av_frame_alloc())
        flt_frame[0].format = opar.format
        flt_frame[0].width  = opar.width
        flt_frame[0].height = opar.height
        FFmpeg.av_frame_get_buffer(flt_frame[0], 32)
        ifmt, iw, ih = opar.format, opar.width, opar.height
    end
    -- override parameter
    if ofile.c and ofile.c.v then opar.codec_id = ofile.c.v end
    local codec = FFmpeg.avcodec_find_encoder(opar.codec_id)
    if ofile.pix_fmt then
        opar.format = ofile.pix_fmt
    elseif codec.pix_fmts ~= nil then
        opar.format = _OPT.choose_pix_fmt(codec, opar.format)
    end
    if ofile.s and ofile.s.v then
        local w, h = string.match(ofile.s.v, '^(%d*)x(%d*)$')
        assert(w and h, ofile.s.v)
        opar.width, opar.height = tonumber(w), tonumber(h)
    end
    local swsctx, sws_frame
    if ifmt ~= opar.format or iw ~= opar.width or  ih ~= opar.height then
        swsctx = FFmpeg.sws_getCachedContext(nil, iw, ih,ifmt,
        opar.width, opar.height, opar.format,
        FFmpeg.SWS_BICUBIC, nil, nil, nil)
        FFmpeg.av_log(fmtctx(ofile), FFmpeg.AV_LOG_WARNING, string.format('swsctx %s:%dx%d->%s:%dx%d',
        ffi.string(FFmpeg.av_get_pix_fmt_name(ifmt)), iw, ih,
        ffi.string(FFmpeg.av_get_pix_fmt_name(opar.format)), opar.width, opar.height))
        sws_frame = ffi.new('AVFrame*[1]', FFmpeg.av_frame_alloc())
        ffi.gc(sws_frame, FFmpeg.av_frame_free)
        sws_frame[0].format = opar.format
        sws_frame[0].width  = opar.width
        sws_frame[0].height = opar.height
        FFmpeg.av_frame_get_buffer(sws_frame[0], 32)
    end
    --创建编码的AVCodecContext
    local avctx = ffi.new('AVCodecContext*[1]', FFmpeg.avcodec_alloc_context3(codec))
    ffi.gc(avctx, FFmpeg.avcodec_free_context)

    avctx[0].time_base.num, avctx[0].time_base.den = ost.time_base.num, ost.time_base.den
    local ret = FFmpeg.avcodec_parameters_to_context(avctx[0], opar)
    FFmpeg.assert(ret, 'avcodec_parameters_to_context')
    local dict = _OPT.codec_dict(ofile, opar.codec_id, fmtctx(ofile), ost, codec)
    local ret = FFmpeg.avcodec_open2(avctx[0], codec, dict)
    FFmpeg.assert(ret, 'avcodec_open2')
    ret = FFmpeg.avcodec_parameters_from_context(opar, avctx[0])
    FFmpeg.assert(ret, 'avcodec_parameters_from_context')

    local packet = ffi.new('AVPacket*[1]', FFmpeg.av_packet_alloc())
    ffi.gc(packet, FFmpeg.av_packet_free)
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
    local name = url(ofile)
    local ctx = fmtctx(ofile, true)
    ofile.m = _OPT.stream_map(ofile)
    for uid, st in ipairs(ofile.m) do
        for sid, info in ipairs(st) do add_stream(ofile, uid, sid, info) end
    end
    FFmpeg.av_dump_format(ctx, 0, name, 1)

    --open the output url, if needed
    if bit.band(ctx.oformat.flags, FFmpeg.AVFMT_NOFILE) == 0 then 
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
        ctx.pb = pb[0]
    end
    local ret = FFmpeg.avformat_write_header(ctx, nil)
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
local function receive_frame(ocell)
    local ifile = _URL[ocell.uid]
    local icell = ifile[ocell.sid]
    local ret = 0
    ::retry::
    ret = FFmpeg.av_read_frame(fmtctx(ifile), icell.packet[0])
    if ret == 0 then
        if icell.packet[0].stream_index + 1 ~= ocell.sid then goto retry end
        ret = FFmpeg.avcodec_send_packet(icell.avctx[0], icell.packet[0])
        if ret == 0 then
            ret = FFmpeg.avcodec_receive_frame(icell.avctx[0], icell.frame[0])
            if ret == FFmpeg.AVERROR_AGAIN then goto retry end
        end
    end
    if ret == 0 and icell.fltctx then
        ret = FFmpeg.av_buffersrc_add_frame_flags(icell.fltctx[0],
        icell.frame[0], FFmpeg.AV_BUFFERSRC_FLAG_KEEP_REF)
        FFmpeg.assert(ret, 'Error while feeding the filtergraph')
    end
    return ret, icell.frame[0]
end
local function transcode_step(ofile)
    local ocell = choose_ocell(ofile)
    if not ocell then return close_ofile(ofile) end

    local ret, frame = receive_frame(ocell)
    if ret == FFmpeg.AVERROR_EOF then  return close_ocell(ocell) end
    FFmpeg.assert(ret, 'decode_frame')

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
    frame.pts, ocell.pts = ocell.pts, ocell.pts+1
    ret = FFmpeg.avcodec_send_frame(ocell.avctx[0], frame)
    FFmpeg.assert(ret, 'avcodec_send_frame')
    while true do
        ret = FFmpeg.avcodec_receive_packet(ocell.avctx[0], ocell.packet[0])
        if ret == FFmpeg.AVERROR_AGAIN then return end
        FFmpeg.assert(ret, 'avcodec_receive_packet')
        ret = FFmpeg.av_write_frame(fmtctx(ofile), ocell.packet[0])
        if ret == FFmpeg.AVERROR_EIO then return close_ofile(ofile) end
        FFmpeg.assert(ret, 'av_write_frame')
    end
end
--------------------------------------------------------------------------------
local _TTY, ret = loadfile('ff-tty.lua')
assert(_TTY, ret)
ret, _TTY = pcall(_TTY, FFmpeg, _OPT)
assert(ret, _TTY)
--------------------------------------------------------------------------------
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
--------------------------------------------------------------------------------
local scuess, reason = pcall(transcode)
assert(scuess, reason)
