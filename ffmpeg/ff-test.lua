local ffi = require'ffi'
local bit = require'bit'
local FFmpeg = loadfile('init.lua')[[Z:\develop\ffmpeg-3.4.2-win64]]

function test()
    local file = FFmpeg.new('centaur_1.mpg')
    print(file:pixel_format_name())
    print(file:duration())
    ascii_frame = file:filter('gray', 'scale=40:12'):read_video_frame():to_ascii()

    print(ascii_frame)
end
local ret, msg = pcall(test)
if not ret then print(msg) end
