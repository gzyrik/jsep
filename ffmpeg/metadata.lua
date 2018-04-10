local ffi = require'ffi'
local bit = require'bit'
local FFmpeg = loadfile('init.lua')[[Z:\develop\ffmpeg-3.4.2-win64]]

local filename = arg[1] or 'centaur_1.mpg'
local fmt_ctx = ffi.new('AVFormatContext*[1]')
local ret = FFmpeg.avformat_open_input(fmt_ctx, filename, nil, nil)
FFmpeg.assert(ret, filename)

local tag = FFmpeg.av_dict_get(fmt_ctx[0].metadata, '', nil, FFmpeg.AV_DICT_IGNORE_SUFFIX)
while nil ~= tag do
    print(ffi.string(tag.key)..'='..ffi.string(tag.value))
    tag = FFmpeg.av_dict_get(fmt_ctx[0].metadata, '', tag, FFmpeg.AV_DICT_IGNORE_SUFFIX)
end
FFmpeg.avformat_close_input(fmt_ctx);
