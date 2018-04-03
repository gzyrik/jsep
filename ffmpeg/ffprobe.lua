local ffi = require('ffi')
local FFmpeg = loadfile('init.lua')[[Z:\Works\justalk\sdk\grape\cranberry\cranberry\external\FFmpeg\3.0.1-win32]]
local C = ffi.C
local function print_error(filename, err)
    local errbuf = ffi.new('char[?]', 128)
    if FFmpeg.av_strerror(err, errbuf, 128) < 0 then errbuf = C.strerror(err) end
    FFmpeg.av_log(nil, FFmpeg.AV_LOG_ERROR, "%s: %s\n", filename, errbuf);
    os.exit(-1)
end
local function setup_find_stream_info_opts(format_context, codec_opts)
    if format_context.nb_streams == 0 then return end
    local opts = ffi.new('AVDictionary*[?]', format_context.nb_streams)
    for i=0,format_context.nb_streams do
        opts[i] = filter_codec_opts(codec_opts, format_context.streams[i].codec.codec_id,
        format_context, format_context.streams[i], nil)
    end
end
local function open_input_file(filename)
    local format_context = ffi.new('AVFormatContext*[1]')
    local ret = FFmpeg.avformat_open_input(format_context, filename, nil, nil)
    if ret < 0 then print_error(filename, ret) end
    ffi.gc(format_context, FFmpeg.avformat_close_input)
    ret = FFmpeg.avformat_find_stream_info(format_context[0], nil)
    if ret < 0 then print_error(filename, ret) end
    -- fill the streams in the format context
    opts = setup_find_stream_info_opts(format_context[0], codec_opts)
    orig_nb_streams = format_context[0].nb_streams
end
local ret, msg = pcall(open_input_file, 'centaur_1.mpg')
if not ret then print(msg) end
