local description={
    f=[[
-f fmt (input/output)
    Force input or output file format.
    The format is normally auto detected for input files
    and guessed from the file extension for output files,
    so this option is not needed in most cases
]],
    i=[[
-i url (input)
    input file url
]],
    y=[[ 
-y (global)
    Overwrite output files without asking.
]],

    n=[[
-n (global)
    Do not overwrite output files, and exit immediately,
    if a specified output file already exists
]],
    pix_fmt=[[
--pix_fmt=[:stream_specifier] format (input/output,per-stream)
]],

    map=[[
--map=,-m [-]input_id[:strm_spc][?][,sync_id[:strm_spc]\] | [linklabl] (output)
    For example, to map ALL streams from the first input file to output
        -i INPUT -map 0 output
    For example, to map the input stream in 'INPUT' identified by "0:1"
    to the (single) output stream in 'out.wav'
        -i INPUT -map 0:1 out.wav
]],

    cocec=[[
--codec,-c[:stream_specifier] codec (input/output,per-stream)
    Select an encoder (when used before an output file) or
    a decoder (when used before an input file) for one or more streams.
    codec is the name of a decoder/encoder or a special value `copy'
    (output only) to indicate that the stream is not to be re-encoded.
    For example
        -i INPUT -c:v libx264 -c:a copy OUTPUT
]],

    t=[[
-t duration (input/output)
    When used as an input option (before -i), 
    limit the duration of data read from the input file.
    When used as an output option (before an output url),
    stop writing the output after its duration reaches duration.
    duration must be a time duration specification, 
    see (ffmpeg-utils)the Time duration section in the ffmpeg-utils(1) manual.
    -to and -t are mutually exclusive and -t has priority
]],

    s=[[
-s[:stream_specifier] size (input/output,per-stream)’
]],
}
local log_options={'',[[
Print help / information / capabilities:
    --hide_banner       Suppress printing banner.
]]}
local global_options={[[
Global options (affect whole program instead of just one file:
    -y                  overwrite output files
    -n                  never overwrite output files
]],[[
Advanced global options:
]]}
local file_options={[[
Per-file main options:
    -f fmt              force format
]],[[
Advanced per-file options:
    --map [-]input_file_id[:stream_specifier][,sync_file_id[:stream_specifier]
                        set input stream mapping
]]}
local video_options={[[
Video options:
    -s size             set frame size (WxH or abbreviation)
]],[[
Advanced Video options:
    --pix_fmt format    set pixel format
]]}
local audio_options={[[
Audio options:
]],[[
Advanced Audio options:
]]}
local subtitle_options={[[
Subtitle options:
]],[[
]]}
local basic=[[
Usage:
[options] [[infile options] -i infile]... {[outfile options] outfiles} ...

Getting help:
    --help=,-h          print basic options
    --help=,-h -opt     print detailed description of option. e.g. -h -y
    --help=,-h category print all options of category. 
                        log, global, file, video, audio, subtitle, demo, or all 
]]
local options = {
    log_options, global_options, file_options, video_options, audio_options, subtitle_options,
    log = log_options,
    global = global_options,
    file   = file_options,
    video  = video_options,
    audio  = audio_options,
    subtitle=subtitle_options,
}
local demo=[[
-fdshow -i"video=FaceTime HD Camera" -fsdl -c:v rawvideo --pix_fmt=yuv420p
-i centaur_1.mpg -f sdl -c:v rawvideo --pix_fmt=yuv420p
-i centaur_1.mpg -f sdl --vcodec=rawvideo --pix_fmt=yuv420p -s 640x480
-i centaur_1.mpg -c:v libx264 out.mp4
]]
local function help(v)
    io.write(basic, '\n')
    if type(v) == 'string' then
        local _, opt = string.match(v, '(-+)(%w*)')
        if not _ then
            opt = options[v]
            if opt then
                io.write(opt[1], '\n', opt[2], '\n')
            elseif v == 'all' then
                for _,v in ipairs(options) do io.write(v[1],'\n', v[2], '\n') end
            elseif v == 'demo' then
                io.write(demo)
            else
                io.write('Invalid category: ', v)
                os.exit(-1)
            end
        else
            opt = description[opt]
            if opt then
                io.write(opt,'\n')
            else
                io.write('Invalid option: ', v)
                os.exit(-1)
            end
        end
    else
        for _,v in ipairs(options) do io.write(v[1], '\n') end
    end
    os.exit(0)
end
local getopt = dofile('getopt.lua')
local iformat, icodec = {}, {} --input format, input codec
local opt = getopt(..., 'i:fc:s:m:h',{
    help='h', map='m', codec='c',
    acodec='c:a', vcodec='c:v',
    i=function(r)
        iformat[#r.i], r.f = r.f, nil
        icodec[#r.i], r.c = r.c, {}
    end,
})
if opt.h then help(opt.h) end
opt.iformat = iformat
opt.icodec = icodec
return opt
