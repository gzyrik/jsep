local SDK, CDEF, CWD = ...
--------------------------------------------------------------------------------
local ffi, bit = require'ffi', require'bit'
if ffi.cset(CDEF) then
    local tmpf = os.tmpname()
    local file = io.open(tmpf, 'w+')
    file:write[[
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavdevice/avdevice.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
    #include <libpostproc/postprocess.h>
    #include <libavutil/avutil.h>
    #include <libavutil/opt.h>
    #include <libavutil/time.h>
    #include <libavutil/imgutils.h>
    #include <libavfilter/avfilter.h>
    #include <libavfilter/buffersrc.h>
    #include <libavfilter/buffersink.h>
    ]]
    file:close()
    local file = io.popen(CWD..'cpp '..SDK..'include'..' '..tmpf)
    local cdef = file:read('*all')
    file:close()
    ffi.cdef(cdef)
    ffi.cdef[[
    int strcmp(const char*, const char*);
    int vsprintf (char*, const char*, va_list);
    const char* strstr(const char*, const char*);
    char *strerror(int err);
    ]]
    ffi.cdump(CDEF)
end
--------------------------------------------------------------------------------
local function load_dlls(file, patten)
    local ret={}
    for line in file:lines() do
        local ok, mod = pcall(ffi.load, line)
        assert(ok, mod)
        local name = string.match(line, patten)
        ret[name] = mod
    end
    file:close()
    return ret
end
local DLL
if jit.os == 'Windows' then
    ffi.cdef[[
    int _chdir(const char *path);
    char *_getcwd(char *buffer,  int maxlen);
    ]]
    local cwd = ffi.new("char[?]", 1024)
    ffi.C._getcwd(cwd, 1024)
    local ret = ffi.C._chdir(SDK..'bin')
    assert(ret == 0, '\nInvalid sdk dir: ' ..SDK)
    DLL = load_dlls(io.popen('dir /B *.dll'), '^(%a*)-%d*%.dll$')
    ffi.C._chdir(cwd)
else
    local suffix = string.match(package.cpath, '.*(%..*)')
    DLL = load_dlls(io.popen('ls '..SDK..'bin/*'..suffix), '^.*lib(%a*).%d*%'..suffix..'$')
end
--------------------------------------------------------------------------------
assert(DLL, '\nInvalid sdk dir: ' ..SDK)
DLL.avcodec.avcodec_register_all()
DLL.avdevice.avdevice_register_all()
DLL.avfilter.avfilter_register_all()
DLL.avformat.av_register_all()
DLL.avformat.avformat_network_init()
--------------------------------------------------------------------------------
local function FFERRTAG(a,b,c,d)
    if type(a)=='string' then a,b,c,d = string.byte(a, 1, 4)
    elseif type(b)=='string' then b,c,d = string.byte(b, 1, 3)
    elseif type(c)=='string' then c,d = string.byte(c, 1, 2) end
    return -bit.bxor(a, bit.lshift(b, 8), bit.lshift(c, 16), bit.lshift(d, 24))
end
local M = {
    AVERROR_INVAL = -22,
    AVERROR_EIO = -5,

    AVERROR_EXIT = FFERRTAG'EXIT',
    AVERROR_EOF = FFERRTAG'EOF ',

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
    AV_LOG_PRINT_LEVEL = 2,

    AVFMT_NOFILE   =    0x0001,

    AVFMT_GLOBALHEADER = 0x0040,
    AVFMT_RAWPICTURE   = 0x0020,

    AV_CODEC_FLAG_GLOBAL_HEADER=bit.lshift(1, 22),

    AVIO_FLAG_READ  = 1,
    AVIO_FLAG_WRITE = 2,
    AVIO_FLAG_READ_WRITE = 3,

    AV_NOPTS_VALUE = 0x8000000000000000LL,

    AV_PKT_FLAG_KEY   =  0x0001,
    AV_PKT_FLAG_CORRUPT = 0x0002, 

    AV_DICT_MATCH_CAS为 = 1,
    AV_DICT_IGNORE_SUFFIX = 2,
    AV_DICT_DONT_STRDUP_KEY = 4,
    AV_DICT_DONT_STRDUP_VAL = 8,
    AV_DICT_DONT_OVERWRITE = 16,
    AV_DICT_APPEND = 32,
    AV_DICT_MULTIKEY = 64,

    SWS_FAST_BILINEAR = 1,
    SWS_BILINEAR = 2,
    SWS_BICUBIC = 4,

    AV_PIX_FMT_FLAG_BE = 0x1,
    AV_PIX_FMT_FLAG_PAL = 0x2,
    AV_PIX_FMT_FLAG_BITSTREAM = 0x4,
    AV_PIX_FMT_FLAG_HWACCEL = 0x8,
    AV_PIX_FMT_FLAG_PLANAR = 0x10,
    AV_PIX_FMT_FLAG_RGB = 0x20,
    AV_PIX_FMT_FLAG_PSEUDOPAL = 0x40,
    AV_PIX_FMT_FLAG_ALPHA = 0x80,
    AV_PIX_FMT_FLAG_BAYER = 0x100,

    AV_CODEC_PROP_INTRA_ONLY = 0x1,
    AV_CODEC_PROP_LOSSY = 0x2,
    AV_CODEC_PROP_LOSSLESS = 0x4,
    AV_CODEC_PROP_REORDER = 0x8,


    AV_CODEC_CAP_DRAW_HORIZ_BAND = 0x1,
    AV_CODEC_CAP_DR1 = 0x2,
    AV_CODEC_CAP_TRUNCATED = 0x8,
    AV_CODEC_CAP_DELAY = 0x20,
    AV_CODEC_CAP_SMALL_LAST_FRAME = 0x40,
    AV_CODEC_CAP_HWACCEL_VDPAU=0x80,
    AV_CODEC_CAP_SUBFRAMES=0x100,
    AV_CODEC_CAP_EXPERIMENTAL =0x200,
    AV_CODEC_CAP_CHANNEL_CONF =0x400,
    AV_CODEC_CAP_FRAME_THREADS=0x1000,
    AV_CODEC_CAP_SLICE_THREADS=0x2000,
    AV_CODEC_CAP_PARAM_CHANGE=0x4000,
    AV_CODEC_CAP_AUTO_THREADS=0x8000,
    AV_CODEC_CAP_VARIABLE_FRAME_SIZE=0x10000,
    AV_CODEC_CAP_AVOID_PROBING=0x20000,
    AV_CODEC_CAP_LOSSLESS = 0x80000000,

    AV_OPT_FLAG_ENCODING_PARAM = 1,
    AV_OPT_FLAG_DECODING_PARAM = 2,
    AV_OPT_FLAG_METADATA       = 4,
    AV_OPT_FLAG_AUDIO_PARAM    = 8,
    AV_OPT_FLAG_VIDEO_PARAM    = 16,
    AV_OPT_FLAG_SUBTITLE_PARAM = 32,
    AV_OPT_FLAG_FILTERING_PARAM= 0x10000,

    AV_OPT_SEARCH_CHILDREN = 1,
    AV_OPT_SEARCH_FAKE_OBJ = 2,
    AV_OPT_ALLOW_NULL = 4,
    AV_OPT_MULTI_COMPONENT_RANGE = 0x1000,


    AVFILTER_FLAG_DYNAMIC_INPUTS = 1,
    AVFILTER_FLAG_DYNAMIC_OUTPUTS= 2,
    AVFILTER_FLAG_SLICE_THREADS = 4,
    AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC=0x10000,
    AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL=0x20000,
    AVFILTER_FLAG_SUPPORT_TIMELINE = 0x30000,
}
if jit.os == 'Windows' then
    M.AVERROR_AGAIN =  -11
else
    M.AVERROR_AGAIN =  -35
end
--------------------------------------------------------------------------------
local function sym(lib, func) return lib[func] end
M.assert = function (err, prefix, ...)
    if err == 0 or err == true then return end
    if type(err) ~= 'number' then
        if err ~= nil then return end
        if type(prefix) ~= 'string' then prefix = '(nil)'
        else prefix = string.format(prefix, ...) end
        M.av_log(nil, M.AV_LOG_FATAL, prefix)
    elseif err < 0 then
        local errbuf = ffi.new('char[?]', 1024)
        if M.av_strerror(err, errbuf, 1024) < 0 then errbuf = ffi.C.strerror(err) end
        if type(prefix) ~= 'string' then prefix = '(nil)'
        else prefix = string.format(prefix, ...) end
        M.av_log(nil, M.AV_LOG_FATAL, "%s: %s\n", prefix, errbuf)
        if err == M.AVERROR_EXIT then os.exit(0) end
    else
        return
    end
    error('', 2)
end
M.error= function (...)
    local idx, avcl = 1
    if type(select(1, ...)) ~= 'string' then avcl,idx = select(1, ...), 2 end
    M.av_log(avcl, M.AV_LOG_FATAL, string.format(select(idx, ...)))
    error('', 2)
end
local function cache(v, e)
    local f
    for _, mod in pairs(DLL) do
        v,f= pcall(sym, mod, e)
        if v then break end
    end
    if not v then return v, f end
--[[
    v = string.match(tostring(f), '<(.-)%b()>')--function return type
    if v then
        if string.byte(v, -1) == 42 then --'*'
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
                if r == nil then return nil end
            elseif v==2 and r < 0 then
                local errbuf = ffi.new('char[?]', 1024)
                if avutil.av_strerror(r, errbuf, 1024) >= 0 then
                    avutil.av_log(nil, M.AV_LOG_ERROR, "%s: %s\n", e, errbuf)
                end
            end
            return r
        end
    end
 ]]
    rawset(M, e, f)
    return f
end
M.foreach = function (func, init)
    if type(func) == 'string' then func = M[func] end
    return function(_, v)
        v = func(v) 
        return v ~= nil and v or nil
    end, nil, init
end
return setmetatable(M, {__index=cache})
