local ffi = require'ffi'
local bit = require'bit'
local ffmpeg_dir = ...
local ffmpeg_bin = ffmpeg_dir .. '/bin'
local ffmpeg_def = ffmpeg_dir .. '/def.lua'
if ffi.cset(ffmpeg_def) then
    local ffmpeg_inc = ffmpeg_dir .. '/include'
    local includes_path = os.tmpname()
    -- Write includes to a temporary file
    local includes_file = io.open(includes_path, 'w+')
    includes_file:write[[
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
    #include <libavutil/avutil.h>
    #include <libavutil/opt.h>
    #include <libavfilter/avfilter.h>
    #include <libavfilter/buffersrc.h>
    #include <libavfilter/buffersink.h>
    ]]
    includes_file:close()

    -- Preprocess header files to get C declarations
    local cpp_output = io.popen('cpp '..ffmpeg_inc..' '..includes_path)
    local def = cpp_output:read('*all')
    cpp_output:close()
    ffi.cdef(def)
    ffi.cdef[[
    int _chdir(const char *path);
    char *_getcwd(char *buffer,  int maxlen);
    char *strerror(int err);
    ]]
    ffi.cdump(ffmpeg_def)
end

local function load_lib(t)
  local err = ''
  for _, name in ipairs(t) do
    local ok, mod = pcall(ffi.load, name)
    if ok then return mod, name end
    err = err .. '\n' .. mod
  end
  error(err)
end

local path = ffi.new("char[?]", 1024)
ffi.C._getcwd(path, 1024)
ffi.C._chdir(ffmpeg_bin)
local avutil = load_lib{'avutil-55'}
local avcodec = load_lib{'avcodec-57'}
local avformat = load_lib{'avformat-57'}
local avfilter = load_lib{'avfilter-6'}
ffi.C._chdir(path)
--------------------------------------------------------------------------------
local M={
    avutil = avutil,
    avcodec = avcodec,
    avformat = avformat,
    avfilter = avfilter,

    AV_LOG_QUIET   = -8,
    AV_LOG_PANIC   = 0,
    AV_LOG_FATAL   = 8,
    AV_LOG_ERROR   = 16,
    AV_LOG_WARNING = 24,
    AV_LOG_INFO    = 32,
    AV_LOG_VERBOSE = 40,
    AV_LOG_DEBUG   = 48,
    AV_LOG_TRACE   = 56,

    AV_LOG_SKIP_REPEATED = 1,

    AVFMT_NOFILE   =    0x0001,

    AVFMT_GLOBALHEADER = 0x0040,
    AVFMT_RAWPICTURE   = 0x0020,

    AV_CODEC_FLAG_GLOBAL_HEADER=bit.lshift(1, 22),

    AVIO_FLAG_READ  = 1,
    AVIO_FLAG_WRITE = 2,
    AVIO_FLAG_READ_WRITE = 3,

    AV_NOPTS_VALUE = 0x8000000000000000,

    AV_PKT_FLAG_KEY   =  0x0001,
    AV_PKT_FLAG_CORRUPT = 0x0002, 
   
    AV_DICT_MATCH_CAS为 = 1,
    AV_DICT_IGNORE_SUFFIX = 2,
    AV_DICT_DONT_STRDUP_KEY = 4,
    AV_DICT_DONT_STRDUP_VAL = 8,
    AV_DICT_DONT_OVERWRITE = 16,
    AV_DICT_APPEND = 32,
    AV_DICT_MULTIKEY = 64
}
local Video = {}
local VideoFrame = {}
local AV_OPT_SEARCH_CHILDREN = 1

--avutil.av_log_set_level(M.AV_LOG_ERROR)
avutil.av_log_set_flags(M.AV_LOG_SKIP_REPEATED)

avcodec.avcodec_register_all();
if avdevice then avdevice.avdevice_register_all() end
avfilter.avfilter_register_all()
avformat.av_register_all()
avformat.avformat_network_init();

local function copy_object(original)
  local copy
  if type(original) == 'table' then
    copy = {}
    for k, v in pairs(original) do
      copy[k] = v
    end
    setmetatable(copy, getmetatable(original))
  else
    copy = original
  end
  return copy
end

local function new_video_frame(ffi_frame)
  local self = {ffi_frame = ffi_frame}
  setmetatable(self, {__index = VideoFrame})

  return self
end

local function create_frame_reader(self)
  local frame_reader = coroutine.create(function()
    local packet = ffi.new('AVPacket[1]')
    avcodec.av_init_packet(packet)
    ffi.gc(packet, avcodec.av_packet_unref)

    local frame = ffi.new('AVFrame*[1]', avutil.av_frame_alloc())
    if frame[0] == 0 then
      error('Failed to allocate frame')
    end
    ffi.gc(frame, function(ptr)
      avutil.av_frame_unref(ptr[0])
      avutil.av_frame_free(ptr)
    end)

    local filtered_frame
    if self.is_filtered then
      filtered_frame = ffi.new('AVFrame*[1]', avutil.av_frame_alloc())
      if filtered_frame[0] == 0 then
        error('Failed to allocate filtered_frame')
      end
      ffi.gc(filtered_frame, function(ptr)
        avutil.av_frame_unref(ptr[0])
        avutil.av_frame_free(ptr)
      end)
    end

    while avformat.av_read_frame(self.format_context[0], packet) == 0 do
      -- Make sure packet is from video stream
      if packet[0].stream_index == self.video_stream_index then
        -- Reset fields in frame
        avutil.av_frame_unref(frame[0])

        local got_frame = ffi.new('int[1]')
        if avcodec.avcodec_decode_video2(self.video_decoder_context, frame[0], got_frame, packet) < 0 then
          error('Failed to decode video frame')
        end

        if got_frame[0] ~= 0 then
          if self.is_filtered then
            -- Push the decoded frame into the filtergraph
            if avfilter.av_buffersrc_add_frame_flags(self.buffersrc_context[0],
              frame[0], avfilter.AV_BUFFERSRC_FLAG_KEEP_REF) < 0
            then
              error('Error while feeding the filtergraph')
            end

            -- Pull filtered frames from the filtergraph
            avutil.av_frame_unref(filtered_frame[0]);
            while avfilter.av_buffersink_get_frame(self.buffersink_context[0], filtered_frame[0]) >= 0 do
              coroutine.yield(filtered_frame[0], 'video')
            end
          else
            coroutine.yield(frame[0], 'video')
          end
        end

      else
        -- TODO: Audio frames
      end
    end
  end)

  return frame_reader
end

---- Opens a video file for reading.
--
-- @string path A relative or absolute path to the video file.
-- @treturn Video
function M.new(path)
  local self = {is_filtered = false}
  setmetatable(self, {__index = Video})

  self.format_context = ffi.new('AVFormatContext*[1]')
  if avformat.avformat_open_input(self.format_context, path, nil, nil) < 0 then
    error('Failed to open video input for ' .. path)
  end

  -- Release format context when collected by the GC
  ffi.gc(self.format_context, avformat.avformat_close_input)

  -- Calculate info about the stream
  if avformat.avformat_find_stream_info(self.format_context[0], nil) < 0 then
    error('Failed to find stream info for ' .. path)
  end

  -- Select video stream
  local decoder = ffi.new('AVCodec*[1]')
  self.video_stream_index = avformat.av_find_best_stream(
    self.format_context[0], avformat.AVMEDIA_TYPE_VIDEO, -1, -1, decoder, 0)
  if self.video_stream_index < 0 then
    error('Failed to find video stream for ' .. path)
  end

  self.video_decoder_context = self.format_context[0].streams[self.video_stream_index].codec

  if avcodec.avcodec_open2(self.video_decoder_context, decoder[0], nil) < 0 then
    error('Failed to open video decoder')
  end

  -- Release decoder context when collected by the GC
  ffi.gc(self.video_decoder_context, avcodec.avcodec_close)

  -- -- Print format info
  -- avformat.av_dump_format(self.format_context[0], 0, path, 0)

  return self
end

--- A Video class.
-- @type Video

---- Sets a filter to apply to the video.
--
-- @string pixel_format_name The name of the desired output pixel format.
-- Pixel names can be found in
-- [pixdesc.c](https://www.ffmpeg.org/doxygen/1.1/pixdesc_8c_source.html).
-- @string[opt='null'] filterchain The filterchain to be applied. Refer to the
-- [libav documentation](https://libav.org/documentation/avfilter.html)
-- for the syntax of this string.
-- @treturn Video A copy of this `Video` with the specified filter set
-- up.
--
-- @usage
-- -- Set up a filter which scales the video to 128x128 pixels, flips it
-- -- horizontally and sets the output pixel format to 24-bit RGB:
-- video = video:filter('rgb24', 'scale=128x128,hflip')
function Video:filter(pixel_format_name, filterchain)
  assert(not self.is_filtered)

  local video = copy_object(self)

  filterchain = filterchain or 'null'
  local buffersrc = avfilter.avfilter_get_by_name('buffer');
  local buffersink = avfilter.avfilter_get_by_name('buffersink');
  local outputs = ffi.new('AVFilterInOut*[1]', avfilter.avfilter_inout_alloc());
  ffi.gc(outputs, avfilter.avfilter_inout_free)
  local inputs = ffi.new('AVFilterInOut*[1]', avfilter.avfilter_inout_alloc());
  ffi.gc(inputs, avfilter.avfilter_inout_free)

  local filter_graph = ffi.new('AVFilterGraph*[1]', avfilter.avfilter_graph_alloc());
  ffi.gc(filter_graph, avfilter.avfilter_graph_free)

  local args = string.format(
    'video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d',
    video.video_decoder_context.width,
    video.video_decoder_context.height,
    tonumber(video.video_decoder_context.pix_fmt),
    video.video_decoder_context.time_base.num,
    video.video_decoder_context.time_base.den,
    video.video_decoder_context.sample_aspect_ratio.num,
    video.video_decoder_context.sample_aspect_ratio.den)

  local buffersrc_context = ffi.new('AVFilterContext*[1]');
  if avfilter.avfilter_graph_create_filter(
    buffersrc_context, buffersrc, 'in', args, nil, filter_graph[0]) < 0
  then
    error('Failed to create buffer source')
  end

  local buffersink_context = ffi.new('AVFilterContext*[1]');
  if avfilter.avfilter_graph_create_filter(
    buffersink_context, buffersink, 'out', nil, nil, filter_graph[0]) < 0
  then
    error('Failed to create buffer sink')
  end

  local pix_fmt = avutil.av_get_pix_fmt(pixel_format_name)
  if pix_fmt == avutil.AV_PIX_FMT_NONE then
    error('Invalid pixel format name: ' .. pixel_format_name)
  end
  local pix_fmts = ffi.new('enum AVPixelFormat[1]', {pix_fmt})
  if avutil.av_opt_set_bin(buffersink_context[0],
    'pix_fmts', ffi.cast('const unsigned char*', pix_fmts),
    1 * ffi.sizeof('enum AVPixelFormat'), AV_OPT_SEARCH_CHILDREN) < 0
  then
    error('Failed to set output pixel format')
  end

  outputs[0].name       = avutil.av_strdup('in');
  outputs[0].filter_ctx = buffersrc_context[0];
  outputs[0].pad_idx    = 0;
  outputs[0].next       = nil;
  inputs[0].name        = avutil.av_strdup('out');
  inputs[0].filter_ctx  = buffersink_context[0];
  inputs[0].pad_idx     = 0;
  inputs[0].next        = nil;

  if avfilter.avfilter_graph_parse_ptr(filter_graph[0], filterchain,
    inputs, outputs, nil) < 0
  then
    error('avfilter_graph_parse_ptr failed')
  end

  if avfilter.avfilter_graph_config(filter_graph[0], nil) < 0 then
    error('avfilter_graph_config failed')
  end

  video.filter_graph = filter_graph
  video.buffersrc_context = buffersrc_context
  video.buffersink_context = buffersink_context
  video.is_filtered = true

  return video
end

---- Gets the video duration in seconds.
function Video:duration()
  return tonumber(self.format_context[0].duration) / 1000000.0
end

---- Gets the name of the video pixel format.
function Video:pixel_format_name()
  return ffi.string(avutil.av_get_pix_fmt_name(self.video_decoder_context.pix_fmt))
end

---- Reads the next video frame.
-- @treturn VideoFrame
function Video:read_video_frame()
  self.frame_reader = self.frame_reader or create_frame_reader(self)

  while true do
    if coroutine.status(self.frame_reader) == 'dead' then
      error('End of stream')
    end

    local ok, frame, frame_type = coroutine.resume(self.frame_reader)

    if not ok then
      error(frame)
    end

    if frame_type == 'video' then
      return new_video_frame(frame)
    end
  end
end

function Video:each_frame(video_callback, audio_callback)
  if audio_callback ~= nil then
    error('Audio frames not supported yet')
  end

  local running = true
  while running do
    local ok, res = pcall(self.read_video_frame, self)
    if ok then
      video_callback(res)
    else
      running = false
    end
  end
end

--- A VideoFrame class.
-- @type VideoFrame

---- Converts the video frame to an ASCII visualisation.
function VideoFrame:to_ascii()
  local frame = self.ffi_frame
  if frame.format ~= avutil.AV_PIX_FMT_GRAY8 then
    error(string.format(
      'Unexpected pixel format "%s", frame_to_ascii requires "%s"',
      ffi.string(avutil.av_get_pix_fmt_name(frame.format)),
      ffi.string(avutil.av_get_pix_fmt_name(avutil.AV_PIX_FMT_GRAY8))))
  end

  local ascii = {}

  for y = 0, (frame.height - 1) do
    for x = 0, (frame.width - 1) do
      local luma = frame.data[0][y * frame.linesize[0] + x]
      if luma > 200 then
        table.insert(ascii, '#')
      elseif luma > 150 then
        table.insert(ascii, '+')
      elseif luma > 100 then
        table.insert(ascii, '-')
      elseif luma > 50 then
        table.insert(ascii, '.')
      else
        table.insert(ascii, ' ')
      end
    end
    table.insert(ascii, '\n')
  end

  return table.concat(ascii, '')
end

M.Video = Video
M.VideoFrame = VideoFrame
local function sym(lib, func) return lib[func] end
M.assert = function (err, prefix)
    if type(err) ~= 'number' then
        if err == nil then error(prefix, 2) end
        return
    end
    if err >= 0 then return end
    local errbuf = ffi.new('char[?]', 1024)
    if avutil.av_strerror(err, errbuf, 1024) < 0 then errbuf = ffi.C.strerror(err) end
    avutil.av_log(nil, M.AV_LOG_ERROR, "%s: %s\n", prefix, errbuf)
    error(ffi.string(errbuf), 2)
end
local function cache(v, e)
    local v,f= pcall(sym, avutil, e)
    if not v then v,f = pcall(sym, avcodec, e) end
    if not v then v,f = pcall(sym, avformat, e) end
    if not v then v,f = pcall(sym, avfilter, e) end
--[[
    assert(v, f)
    v = string.match(tostring(f), '<(.-)%b()>')--function return type
    if v then
        if string.byte(v, -1) == 42 then
            v = 1
        elseif v == 'int ' then
            v = 2
        else
            v = nil
        end
    end
    if v then
        local func = f
        f = function (...)
            local r = func(...)
            if v==1 then
                assert(r~=nil, e..' return nil')
            elseif v==2 and r < 0 then
                panic(e, r)
            end
            return r
        end
    end
 ]]
    rawset(M, e, f)
    return f
end
return setmetatable(M, {__index=cache})
