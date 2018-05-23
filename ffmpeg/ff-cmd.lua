local ffi,bit,FFmpeg,opt = require'ffi', require'bit', ...
----------------------------------------------------------------------------------
local function btest(a, ...) return bit.band(a, ...) ~= 0 end
local function berase(a, ...) return bit.band(a, bit.bnot(bit.bor(...))) end
local function ffistr(s) return s ~= nil and ffi.string(s) or '' end
local function show_banner()
    local indent = '  '
    local libs= 'avutil avcodec avformat avdevice avfilter swscale swresample postproc'
    FFmpeg.av_log(nil, FFmpeg.AV_LOG_INFO, "%s Copyright (c) 2003-2018 the FFmpeg developers\n", jit and jit.version or _VERSION)
    local a = string.format("%sconfiguration: %s\n", indent, ffi.string(FFmpeg.avformat_configuration()));
    FFmpeg.av_log(nil, FFmpeg.AV_LOG_INFO, a);
    for b in string.gmatch(libs, "%w+") do
        a = FFmpeg[b..'_version']
        if a then
            a = a()
            a = string.format("%s%-11s %2d.%3d.%3d\n", indent, b,
            bit.rshift(a,16), bit.rshift(bit.band(a,0x00FF00),8), bit.band(a,0xFF))
            FFmpeg.av_log(nil, FFmpeg.AV_LOG_INFO, a)
        end
    end
end

local function get_codecs_sorted()
    local codec_desc={}
    for desc in FFmpeg.foreach'avcodec_descriptor_next' do
        table.insert(codec_desc, desc)
    end
    table.sort(codec_desc, function(a, b)
        return a.type == b.type and ffi.C.strcmp(a.name, b.name) < 0 or a.type < b.type
    end)
    return codec_desc
end
local function next_codec_for_id(codec_id, encoder)
    return function(encoder, codec)
        repeat
            codec = FFmpeg.av_codec_next(codec)
            if codec == nil then return nil end
        until codec.id == codec_id and
        0 ~= (encoder and FFmpeg.av_codec_is_encoder(codec) or FFmpeg.av_codec_is_decoder(codec))
        return codec
    end, encoder, nil
end
local function get_media_type_char(type)
    if type == FFmpeg.AVMEDIA_TYPE_VIDEO then     return 'V'
    elseif type == FFmpeg.AVMEDIA_TYPE_AUDIO then return 'A'
    elseif type == FFmpeg.AVMEDIA_TYPE_DATA then  return 'D'
    elseif type == FFmpeg.AVMEDIA_TYPE_SUBTITLE then   return 'S'
    elseif type == FFmpeg.AVMEDIA_TYPE_ATTACHMENT then return 'T'
    else return '?' end
end
local function get_media_type_name(type)
    if type == FFmpeg.AVMEDIA_TYPE_VIDEO then     return 'video'
    elseif type == FFmpeg.AVMEDIA_TYPE_AUDIO then return 'audio'
    elseif type == FFmpeg.AVMEDIA_TYPE_DATA then  return 'data'
    elseif type == FFmpeg.AVMEDIA_TYPE_SUBTITLE then   return 'subtitle'
    elseif type == FFmpeg.AVMEDIA_TYPE_ATTACHMENT then return 'attachment'
    else return '?' end
end

local function  show_help_children(class, flags)
    if class.option ~= nil then
        FFmpeg.av_opt_show2(ffi.new('void*[1]', ffi.cast('void*', class)), nil, flags, 0)
        io.write("\n")
    end

    for child in FFmpeg.foreach(function(child)
        return FFmpeg.av_opt_child_class_next(class, child)
    end) do
        show_help_children(child, flags)
    end
end
local function opt_loglevel(v)
    local log_levels={
        quiet = FFmpeg.AV_LOG_QUIET,
        panic = FFmpeg.AV_LOG_PANIC,
        fatal = FFmpeg.AV_LOG_FATAL,
        error = FFmpeg.AV_LOG_ERROR,
        warning= FFmpeg.AV_LOG_WARNING,
        info   = FFmpeg.AV_LOG_INFO,
        verbose= FFmpeg.AV_LOG_VERBOSE,
        debug  = FFmpeg.AV_LOG_DEBUG,
        trace = FFmpeg.AV_LOG_TRACE
    }
    local oldflags, oldlevel = FFmpeg.av_log_get_flags(), FFmpeg.av_log_get_level()
    local flags, level = oldflags, oldlevel
    for p,a in string.gmatch(v, "([-+=]*)(%w+)") do
        if a == 'repeat' then
            if p == '-' then
                flags = bit.band(flags, bit.bnot(FFmpeg.AV_LOG_SKIP_REPEATED))
            else
                flags = bit.bor(flags, FFmpeg.AV_LOG_SKIP_REPEATED)
            end
        elseif a == 'level' then
            if p == '-' then
                flags = bit.band(flags, bit.bnot(FFmpeg.AV_LOG_PRINT_LEVEL))
            else
                flags = bit.bor(flags, FFmpeg.AV_LOG_PRINT_LEVEL)
            end
        else
            local v = log_levels[a] or tonumber(a)
            if not v then
                FFmpeg.av_log(nil, FFmpeg.AV_LOG_FATAL,
                "Invalid loglevel \"%s\". Possible levels are numbers or:\n", a)
                for k,_ in pairs(log_levels) do
                    FFmpeg.av_log(nil, FFmpeg.AV_LOG_FATAL, "\"%s\"\n", k);
                end
                os.exit(-1)
            end
            if p == '+' then
                level = level + v
            elseif p == '-' then
                level = level - v
            else
                level = v
            end
        end
    end
    if flags ~= oldflags then FFmpeg.av_log_set_flags(flags) end
    if level ~= oldlevel then FFmpeg.av_log_set_level(level) end
end
local function show_protocols()
    local opaque = ffi.new('void*[1]')
    io.write("Supported file protocols:\n", "Input:\n")
    for name in FFmpeg.foreach(function (name)
        return FFmpeg.avio_enum_protocols(opaque, 0)
    end) do
        io.write('  ', ffi.string(name), '\n')
    end
    io.write("Output:\n")
    for name in FFmpeg.foreach(function(name)
        return FFmpeg.avio_enum_protocols(opaque, 1)
    end) do
        io.write('  ', ffi.string(name), '\n')
    end
end
local function print_codecs(encoder, pattern)
    io.write(encoder and "Encoders" or "Decoders",":\n",
    "V..... = Video\n",
    "A..... = Audio\n",
    "S..... = Subtitle\n",
    ".F.... = Frame-level multithreading\n",
    "..S... = Slice-level multithreading\n",
    "...X.. = Codec is experimental\n",
    "....B. = Supports draw_horiz_band\n",
    ".....D = Supports direct rendering method 1\n",
    "-------\n")
    local special = type(pattern) == 'string' and string.len(pattern) > 0
    for _, desc in ipairs(get_codecs_sorted()) do
        for codec in next_codec_for_id(desc.id, encoder) do
            local name, long_name = ffi.string(codec.name), ffistr(codec.long_name)
            if special and not string.find(name..long_name, pattern) then goto continue  end

            io.write(get_media_type_char(desc.type),
            btest(codec.capabilities, FFmpeg.AV_CODEC_CAP_FRAME_THREADS) and "F" or ".",
            btest(codec.capabilities, FFmpeg.AV_CODEC_CAP_SLICE_THREADS) and "S" or ".",
            btest(codec.capabilities, FFmpeg.AV_CODEC_CAP_EXPERIMENTAL)  and "X" or ".",
            btest(codec.capabilities, FFmpeg.AV_CODEC_CAP_DRAW_HORIZ_BAND)and "B" or ".",
            btest(codec.capabilities, FFmpeg.AV_CODEC_CAP_DR1)           and "D" or ".",
            string.format(" %-20s ", name), long_name,
            ffi.C.strcmp(codec.name, desc.name) == 0 and '\n'
            or string.format(" (codec %s)\n", ffi.string(desc.name)))

            ::continue::
        end
    end
end
local function show_codecs(pattern)
    -- print decoders/encoders when there's more than one or their
    -- names are different from codec name
    local function print_next_codec(desc, encoder)
        local names, found={},nil
        for codec in next_codec_for_id(desc.id, encoder) do
            if not found and ffi.C.strcmp(codec.name, desc.name) ~= 0 then found = true end
            table.insert(names, ffi.string(codec.name))
        end
        if found then 
            return string.format(" (%s: %s)", encoder and "encoders" or "decoders", table.concat(names,' '))
        end
        return ''
    end
    io.write("Codecs:\n",
    "D..... = Decoding supported\n",
    ".E.... = Encoding supported\n",
    "..V... = Video codec\n",
    "..A... = Audio codec\n",
    "..S... = Subtitle codec\n",
    "...I.. = Intra frame-only codec\n",
    "....L. = Lossy compression\n",
    ".....S = Lossless compression\n",
    "-------\n");
    local special = type(pattern) == 'string' and string.len(pattern) > 0
    for _, desc in ipairs(get_codecs_sorted()) do
        local name, long_name = ffi.string(desc.name), ffistr(desc.long_name)
        if ffi.C.strstr(desc.name, "_deprecated") ~= nil then goto continue end
        if special and not string.find(name..long_name, pattern) then goto continue  end

        io.write(
        FFmpeg.avcodec_find_decoder(desc.id) ~= nil and "D" or ".",
        FFmpeg.avcodec_find_encoder(desc.id) ~= nil and "E" or ".",
        get_media_type_char(desc.type),
        btest(desc.props, FFmpeg.AV_CODEC_PROP_INTRA_ONLY) and "I" or ".",
        btest(desc.props, FFmpeg.AV_CODEC_PROP_LOSSY)      and "L" or ".",
        btest(desc.props, FFmpeg.AV_CODEC_PROP_LOSSLESS)   and "S" or ".",
        string.format(" %-20s ", name),
        long_name,
        print_next_codec(desc, false),
        print_next_codec(desc, true),
        '\n')

        ::continue::
    end
end
local function show_pix_fmts(pattern)
    io.write("Pixel formats:\n",
    "I.... = Supported Input  format for conversion\n",
    ".O... = Supported Output format for conversion\n",
    "..H.. = Hardware accelerated format\n",
    "...P. = Paletted format\n",
    "....B = Bitstream format\n",
    "FLAGS NAME            NB_COMPONENTS BITS_PER_PIXEL\n",
    "------\n");

    local special = type(pattern) == 'string' and string.len(pattern) > 0
    for pix_desc in FFmpeg.foreach'av_pix_fmt_desc_next' do
        local name, pix_fmt = ffi.string(pix_desc.name), nil
        if special and not string.find(name, pattern) then goto continue  end

        pix_fmt = FFmpeg.av_pix_fmt_desc_get_id(pix_desc)
        io.write(
        FFmpeg.sws_isSupportedInput (pix_fmt) ~= 0 and 'I' or '.',
        FFmpeg.sws_isSupportedOutput(pix_fmt) ~= 0 and 'O' or '.',
        btest(pix_desc.flags, FFmpeg.AV_PIX_FMT_FLAG_HWACCEL)   and 'H' or '.',
        btest(pix_desc.flags, FFmpeg.AV_PIX_FMT_FLAG_PAL)       and 'P' or '.',
        btest(pix_desc.flags, FFmpeg.AV_PIX_FMT_FLAG_BITSTREAM) and 'B' or '.',
        string.format(" %-16s       %d            %2d", name,
        pix_desc.nb_components, FFmpeg.av_get_bits_per_pixel(pix_desc)), '\n');

        ::continue::
    end
end
local function show_filters(pattern)
    io.write("Filters:\n",
    "  A = Audio input/output\n",
    "  V = Video input/output\n",
    "  N = Dynamic number and/or type of input/output\n",
    "  | = Source or sink filter\n",
    "T.. = Timeline support\n",
    ".S. = Slice threading\n",
    "..C = Command support\n",
    "----\n");
    local special = type(pattern) == 'string' and string.len(pattern) > 0
    for filter in FFmpeg.foreach'avfilter_next' do
        local pad, desc, j, first = filter.inputs, {}, 0, true
        local name, long_name = ffi.string(filter.name), ffistr(filter.description)
        if special and not string.find(name .. long_name, pattern) then goto continue  end

        ::output::
        while pad ~= nil and FFmpeg.avfilter_pad_get_name(pad, j) ~= nil do
            table.insert(desc, get_media_type_char(FFmpeg.avfilter_pad_get_type(pad, j)))
            j = j+ 1
        end
        if j == 0 then
            table.insert(desc,
            btest(filter.flags,
            first and FFmpeg.AVFILTER_FLAG_DYNAMIC_INPUTS or FFmpeg.AVFILTER_FLAG_DYNAMIC_OUTPUTS)
            and 'N' or '|')
        end
        if first then
            pad, j, first=filter.outputs, 0, false
            table.insert(desc, '->')
            goto output
        end
        io.write(
        btest(filter.flags, FFmpeg.AVFILTER_FLAG_SUPPORT_TIMELINE) and 'T' or '.',
        btest(filter.flags, FFmpeg.AVFILTER_FLAG_SLICE_THREADS)    and 'S' or '.',
        filter.process_command ~= nil                              and 'C' or '.',
        string.format(' %-17s %-10s ', name, table.concat(desc,'')),
        long_name, '\n')

        ::continue::
    end
end
local function show_formats_devices(pattern, device_only, mux_only, demux_only)
    local function AV_IS_INPUT_DEVICE(category)
        return category == FFmpeg.AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT
        or category == FFmpeg.AV_CLASS_CATEGORY_DEVICE_AUDIO_INPUT
        or category == FFmpeg.AV_CLASS_CATEGORY_DEVICE_INPUT
    end
    local function AV_IS_OUTPUT_DEVICE(category)
        return category == FFmpeg.AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT
        or category == FFmpeg.AV_CLASS_CATEGORY_DEVICE_AUDIO_OUTPUT
        or category == FFmpeg.AV_CLASS_CATEGORY_DEVICE_OUTPUT
    end
    local function is_device(avclass)
        return avclass ~= nil and (AV_IS_INPUT_DEVICE(avclass.category) or AV_IS_OUTPUT_DEVICE(avclass.category))
    end
    io.write(device_only and "Devices:" or "File formats:",'\n',
    "D. = Demuxing supported\n",
    ".E = Muxing supported\n",
    "---\n");
    local special = type(pattern) == 'string' and string.len(pattern) > 0
    local last_name, ofmt, ifmt = "000";
    while true do
        local name, long_name, decode, encode
        if not demux_only then
            ofmt = FFmpeg.av_oformat_next(ofmt)
            while ofmt ~= nil do
                if not device_only or is_device(ofmt.priv_class) then
                    if (not name or ffi.C.strcmp(ofmt.name, name) < 0) and
                        ffi.C.strcmp(ofmt.name, last_name) > 0  then
                        name, long_name, encode = ofmt.name, ofmt.long_name, true
                    end
                end
                ofmt = FFmpeg.av_oformat_next(ofmt)
            end
        end
        if not mux_only then
            ifmt = FFmpeg.av_iformat_next(ifmt)
            while ifmt ~= nil do
                if not device_only or is_device(ifmt.priv_class) then
                    if (not name or ffi.C.strcmp(ifmt.name, name) < 0) and
                        ffi.C.strcmp(ifmt.name, last_name) > 0 then
                        name, long_name, encode = ifmt.name, ifmt.long_name, false 
                    end
                    if name and ffi.C.strcmp(ifmt.name, name) == 0 then
                        decode = true
                    end
                end
                ifmt = FFmpeg.av_iformat_next(ifmt)
            end
        end
        if not name then break end
        last_name = name

        name, long_name = ffi.string(name), ffistr(long_name)
        if special and not string.find(name .. long_name, pattern) then goto continue  end
        io.write(
        decode and 'D' or '.',
        encode and 'E' or '.',
        string.format(' %-15s ', name),
        long_name,
        '\n')
        ::continue::
    end
end
local function PRINT_CODEC_SUPPORTED(field, list_name, get_name)
    if field ~= nil then
        io.write("    Supported ", list_name, ":")
        local str, i = get_name(field[0]), 1
        while str do
            io.write(' ', str)
            str, i= get_name(field[i]), i+1
        end
        io.write("\n")
    end
end
local function print_codec(c)
    local encoder = FFmpeg.av_codec_is_encoder(c) ~= 0

    io.write(encoder and "Encoder " or "Decoder ", ffi.string(c.name),
    c.long_name == nil and '' or '['..ffi.string(c.long_name)..']',
    ':\n')
    local theadflags = bit.bor(FFmpeg.AV_CODEC_CAP_FRAME_THREADS,
    FFmpeg.AV_CODEC_CAP_SLICE_THREADS, FFmpeg.AV_CODEC_CAP_AUTO_THREADS)
    local caps={
        horizband = FFmpeg.AV_CODEC_CAP_DRAW_HORIZ_BAND,
        dr1 = FFmpeg.AV_CODEC_CAP_DR1,
        trunc = FFmpeg.AV_CODEC_CAP_TRUNCATED,
        delay = FFmpeg.AV_CODEC_CAP_DELAY,
        small = FFmpeg.AV_CODEC_CAP_SMALL_LAST_FRAME,
        subframes = FFmpeg.AV_CODEC_CAP_SUBFRAMES,
        exp = FFmpeg.AV_CODEC_CAP_EXPERIMENTAL,
        chconf = FFmpeg.AV_CODEC_CAP_CHANNEL_CONF,
        paramchange = FFmpeg.AV_CODEC_CAP_PARAM_CHANGE,
        variable = FFmpeg.AV_CODEC_CAP_VARIABLE_FRAME_SIZE,
        threads = theadflags,
    }
    io.write("    General capabilities: ")
    if c.capabilities == 0 then
        io.write("none")
    else
        for k, v in pairs(caps) do
            if btest(c.capabilities, v) then io.write(k, ' ') end
        end
    end
    io.write("\n")
    if c.type == FFmpeg.AVMEDIA_TYPE_VIDEO or c.type == FFmpeg.AVMEDIA_TYPE_AUDIO  then
        io.write("    Threading capabilities: ");
        local cap = bit.band(c.capabilities, theadflags)
        if cap == bit.bor(FFmpeg.AV_CODEC_CAP_FRAME_THREADS, FFmpeg.AV_CODEC_CAP_SLICE_THREADS) then
            io.write("frame and slice")
        elseif cap == FFmpeg.AV_CODEC_CAP_FRAME_THREADS then io.write("frame")
        elseif cap == FFmpeg.AV_CODEC_CAP_SLICE_THREADS then io.write("slice")
        elseif cap == FFmpeg.AV_CODEC_CAP_AUTO_THREADS then io.write("auto")
        else io.write("none") end
        io.write("\n");
    end
    PRINT_CODEC_SUPPORTED(c.supported_framerates, "framerates", function(v)
        if v.num == 0 then return nil end
        return v.num ..'/'..v.den
    end)
    PRINT_CODEC_SUPPORTED(c.pix_fmts, "pixel formats", function(v)
        if v == FFmpeg.AV_PIX_FMT_NONE then return nil end
        return ffi.string(FFmpeg.av_get_pix_fmt_name(v))
    end)
    PRINT_CODEC_SUPPORTED(c.supported_samplerates, "sample rates", function(v)
        if v == 0 then return nil end
        return tostring(v)
    end)
    PRINT_CODEC_SUPPORTED(c.sample_fmts,"sample formats", function(v)
        if v == FFmpeg.AV_SAMPLE_FMT_NONE then return nil end
        return ffi.string(FFmpeg.av_get_sample_fmt_name(v))
    end)
    PRINT_CODEC_SUPPORTED(c.channel_layouts, "channel layouts", function(v)
        if v == 0 then return nil end
        local name = ffi.new('char[128]')
        FFmpeg.av_get_channel_layout_string(name, 128, 0, v)
        return ffi.string(name)
    end)
    if c.priv_class ~= nil then
        show_help_children(c.priv_class,
        bit.bor(FFmpeg.AV_OPT_FLAG_ENCODING_PARAM, FFmpeg.AV_OPT_FLAG_DECODING_PARAM))
    end
end
local function show_help_codec(name, encoder)
    local codec, printed
    if encoder then
        codec = FFmpeg.avcodec_find_encoder_by_name(name)
    else
        codec = FFmpeg.avcodec_find_decoder_by_name(name)
    end
    if codec ~= nil then return print_codec(codec) end
    local desc = FFmpeg.avcodec_descriptor_get_by_name(name)
    if desc == nil then FFmpeg.error("Codec '%s' is not recognized by FFmpeg.\n", name) end

    for codec in next_codec_for_id(desc.id, codec, encoder) do
        printed = true
        print_codec(codec)
    end
    if not printed then
        FFmpeg.av_log(nil, FFmpeg.AV_LOG_ERROR,
        "Codec '%s' is known to FFmpeg, "..
        "but no %s for it are available. FFmpeg might need to be "..
        "recompiled with additional external libraries.\n",
        name, encoder and "encoders" or "decoders")
    end
end
local function show_help_demuxer(name)
    local fmt = FFmpeg.av_find_input_format(name)
    if fmt == nil then FFmpeg.error("Unknown format '%s'.\n", name) end

    io.write("Demuxer ", ffi.string(fmt.name),
    fmt.long_name == nil and '' or '['..ffi.string(fmt.long_name)..']',
    ':\n')

    if fmt.extensions ~= nil then
        io.write("    Common extensions: ", ffi.string(fmt.extensions), '\n');
    end

    if fmt.priv_class ~= nil then
        show_help_children(fmt.priv_class, FFmpeg.AV_OPT_FLAG_DECODING_PARAM)
    end
end
local function show_help_muxer(name)
    local fmt = FFmpeg.av_guess_format(name, nil, nil)
    if fmt == nil then FFmpeg.error("Unknown format '%s'.\n", name) end

    io.write("Muxer ", ffi.string(fmt.name),
    fmt.long_name == nil and '' or '['..ffi.string(fmt.long_name)..']',
    ':\n')

    if fmt.extensions ~= nil then
        io.write("    Common extensions: ", ffi.string(fmt.extensions), '\n');
    end
    if fmt.mime_type ~= nil then
        io.write("    Mime type: ", ffi.string(fmt.mime_type), '\n');
    end
    for _, codec in ipairs{'video_codec','audio_codec', 'subtitle_codec'} do
        if fmt[codec] ~= FFmpeg.AV_CODEC_ID_NONE then
            local desc = FFmpeg.avcodec_descriptor_get(fmt[codec])
            if desc ~= nil then
                io.write('    Default ', codec, ': ', ffi.string(desc.name), '\n')
            end
        end
    end
    if fmt.priv_class ~= nil then
        show_help_children(fmt.priv_class, FFmpeg.AV_OPT_FLAG_ENCODING_PARAM)
    end
end
local function show_help_filter(name)
    local f = FFmpeg.avfilter_get_by_name(name)
    if f == nil then FFmpeg.error("Unknown filter '%s'.\n", name) end

    io.write("Filter ", ffi.string(f.name),'\n')
    if f.description ~= nil then
        io.write('  ', ffi.string(f.description), '\n')
    end
    if btest(f.flags, FFmpeg.AVFILTER_FLAG_SLICE_THREADS) then
        io.write("    slice threading supported\n")
    end

    io.write("    Inputs:\n");
    local count = FFmpeg.avfilter_pad_count(f.inputs);
    for i=0, count-1 do
        io.write(string.format("       #%d: %s (%s)\n",
        i, ffi.string(FFmpeg.avfilter_pad_get_name(f.inputs, i)),
        get_media_type_name(FFmpeg.avfilter_pad_get_type(f.inputs, i))))
    end
    if btest(f.flags, FFmpeg.AVFILTER_FLAG_DYNAMIC_INPUTS) then
        io.write("        dynamic (depending on the options)\n")
    elseif count == 0 then
        io.write("        none (source filter)\n")
    end

    io.write("    Outputs:\n")
    count = FFmpeg.avfilter_pad_count(f.outputs)
    for i=0, count-1 do
        io.write(string.format("       #%d: %s (%s)\n",
        i, ffi.string(FFmpeg.avfilter_pad_get_name(f.outputs, i)),
        get_media_type_name(FFmpeg.avfilter_pad_get_type(f.outputs, i))))
    end
    if btest(f.flags, FFmpeg.AVFILTER_FLAG_DYNAMIC_OUTPUTS) then
        io.write("        dynamic (depending on the options)\n")
    elseif count == 0 then
        io.write("        none (sink filter)\n")
    end

    if f.priv_class ~= nil then
        show_help_children(f.priv_class,
        bit.bor(FFmpeg.AV_OPT_FLAG_VIDEO_PARAM,FFmpeg.AV_OPT_FLAG_FILTERING_PARAM,FFmpeg.AV_OPT_FLAG_AUDIO_PARAM))
    end
    if btest(f.flags, FFmpeg.AVFILTER_FLAG_SUPPORT_TIMELINE) then
        io.write("This filter has support for timeline through the 'enable' option.\n");
    end
end
----------------------------------------------------------------------------------
if opt.v then opt_loglevel(opt.v) end
if not opt.hide_banner then show_banner() end
if opt.protocols then show_protocols() end
if opt.filters then show_filters(opt.filters) end
if opt.pix_fmts then show_pix_fmts(opt.pix_fmts) end

if opt.formats      then show_formats_devices(opt.formats, false)
elseif opt.muxers   then show_formats_devices(opt.muxers,  false, true)
elseif opt.demuxers then show_formats_devices(opt.demuxers,false, false, true)
elseif opt.devices  then show_formats_devices(opt.devices, true) end

if opt.codecs then show_codecs(opt.codecs)
elseif opt.decoders then print_codecs(false, opt.decoders)
elseif opt.encoders then print_codecs(true, opt.encoders) end

if opt.h then
    local topic, name = string.match(opt.h, '(%w+)=(%w+)')
    if string.len(name) == 0 then FFmpeg.error("No %s name specified.\n", topic) end

    local strbuf = ffi.new('char[?]', 1024)
    FFmpeg.av_log_set_callback(ffi.cast("void (*)(void*, int, const char*, va_list)",
    function(ptr, level, fmt, vl)
        ffi.C.vsprintf(strbuf, fmt, vl)
        io.write(ffi.string(strbuf))
    end))
    if topic == "decoder" then
        show_help_codec(name, false)
    elseif topic == "encoder" then
        show_help_codec(name, true)
    elseif topic == "demuxer" then
        show_help_demuxer(name)
    elseif topic == "muxer" then
        show_help_muxer(name)
    elseif topic == "filter" then
        show_help_filter(name)
    else
        io.stderr:write('Invalid topic: ', opt.h, '\n')
    end
    os.exit(0)
end
