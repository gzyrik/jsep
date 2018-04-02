local ffmpeg = loadfile('init.lua')
local ffmpeg = ffmpeg[[Z:\Works\justalk\sdk\grape\cranberry\cranberry\external\FFmpeg\3.0.1-win32]]
function test()
    local file = ffmpeg.new('centaur_1.mpg')
    print(file:pixel_format_name())
    print(file:duration())
    ascii_frame = file:filter('gray', 'scale=40:12'):read_video_frame():to_ascii()

    print(ascii_frame)
end
local ret, msg = pcall(test)
if not ret then print(msg) end
