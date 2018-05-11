local description={
    v=[[
-v [flags+]loglevel
    Set logging level and flags used by the library.
    The optional flags prefix can consist of the following values:

    `repeat'
    Indicates that repeated log output should not be compressed to the first
    line and the "Last message repeated n times" line will be omitted.

    `level'
    Indicates that log output should add a [level] prefix to each message
    line. This can be used as an alternative to log coloring,
    e.g. when dumping the log to file.

    Flags can also be used alone by adding a '+'/'-' prefix to set/reset
    a single flag without affecting other flags or changing loglevel.
    When setting both flags and loglevel, a '+' separator is expected between
    the last flags value and before loglevel.
    loglevel is a string or a number containing one of the following values:

    `quiet, -8'
    Show nothing at all; be silent.

    `panic, 0'
    Only show fatal errors which could lead the process to crash,
    such as an assertion failure. This is not currently used for anything.

    `fatal, 8'
    Only show fatal errors.
    These are errors after which the process absolutely cannot continue.

    `error, 16'
    Show all errors, including ones which can be recovered from.

    `warning, 24'
    Show all warnings and errors.
    Any message related to possibly incorrect or unexpected events will be shown.

    `info, 32'
    Show informative messages during processing.
    This is in addition to warnings and errors. This is the default value.

    `verbose, 40'
    Same as info, except more verbose.

    `debug, 48'
    Show everything, including debugging information.

    `trace, 56'

    For example to enable repeated log output,
    add the level prefix, and set loglevel to verbose:
        ffmpeg -loglevel repeat+level+verbose -i input output

    Another example that enables repeated log output without affecting current
    state of level prefix flag or loglevel:
        ffmpeg [...] -loglevel repeat
]],
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
--pix_fmt [:stream_specifier] format (input/output,per-stream)
]],

    map=[[
--map,-m [-]input_id[:strm_spc][?][,sync_id[:strm_spc]\] | [linklabl] (output)
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
    hide_banner=[[suppress printing banner]],
}
local info_options={'',[[
Print help / information / capabilities:
    -formats [pattern]  show available formats
    -muxers  [pattern]  show available muxers
    -demuxers [pattern] show available demuxers
    -devices [pattern]  show available devices
    -codecs [pattern]   show available codecs
    -decoders [pattern] show available decoders
    -encoders [pattern] show available encoders
    -filters [pattern]  show available filters
    -pix_fmts [pattern] show available pixel formats
    -bsfs               show available bit stream filters
    -protocols          show available protocols
    -layouts            show standard channel layouts
    -sample_fmts        show available audio sample formats
    -colors             show available color names
    -sources device     list sources of the input device
    -sinks device       list sinks of the output device
    -hwaccels           show available HW acceleration methods
]]}
local global_options={[[
Global options affect whole program instead of just one file:
    -v [flags+]loglevel set logging level
    -y                  overwrite output files
    -n                  never overwrite output files
]],[[
Advanced global options:
    -cpuflags flags     force specific cpu flags
    -hide_banner        do not show program banner
]]}
local file_options={[[
Per-file main options:
    -f fmt              force format
    -t duration         record or transcode "duration" seconds of audio/video
    -to time_stop       record or transcode stop time
    -fs limit_size      set the limit file size in bytes
    -ss time_off        set the start time offset
    -sseof time_off     set the start time offset relative to EOF
]],[[
Advanced per-file options:
    --map [-]input_file_id[:stream_specifier][,sync_file_id[:stream_specifier]
                        set input stream mapping
]]}
local video_options={[[
Video options:
    -vframes number     set the number of video frames to output
    -r rate             set frame rate (Hz value, fraction or abbreviation)
    -s size             set frame size (WxH or abbreviation)
    -aspect aspect      set aspect ratio (4:3, 16:9 or 1.3333, 1.7777)
    -bits_per_raw_sample number  set the number of bits per raw sample
    -vn                 disable video
    -vcodec codec       force video codec ('copy' to copy stream)
    -s size             set frame size (WxH or abbreviation)
    -ab bitrate         audio bitrate (please use -b:a)
    -b bitrate          video bitrate (please use -b:v)
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
    --help,-h           print basic options
    -h -opt             print detailed description of option. e.g. -h -y
    -h topic=name       print all options for the topic:
                        decoder, encoder, demuxer, muxer, filter
    -h category         print all options of category: 
                        info, global, file, video, audio, subtitle, demo, or all 
]]
local options = {
    info_options, global_options, file_options, video_options, audio_options, subtitle_options,
    info   = info_options,
    global = global_options,
    file   = file_options,
    video  = video_options,
    audio  = audio_options,
    subtitle=subtitle_options,
}
local demo=[[
-f lavfi -i mandelbrot -f sdl
-f lavfi -i testsrc -f sdl
-f lavfi -i testsrc -vf split[a][b];[a]pad=2*iw[1];[b]hflip[2];[1][2]overlay=w  -f sdl
-f dshow -i "video=FaceTime HD Camera" -f sdl -c:v rawvideo -pix_fmt yuv420p
-i centaur_1.mpg -f sdl -c:v rawvideo --pix_fmt=yuv420p
-i centaur_1.mpg -f sdl --vcodec=rawvideo --pix_fmt=yuv420p -s:v 640x480
-i centaur_1.mpg -c:v libx264 out.mp4
]]

local v = ...
io.write(basic, '\n')
if type(v) == 'string' then
    local _, opt = string.match(v, '^(-+)(%w*)$')
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
