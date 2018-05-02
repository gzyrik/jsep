local FFmpeg, _OPT = ...
local ffi = require'ffi'
if jit.os == 'Windows' then
    ffi.cdef[[
    enum {
        CTRL_C_EVENT       = 0,
        CTRL_BREAK_EVENT   = 1,
        CTRL_CLOSE_EVENT   = 2,
        CTRL_LOGOFF_EVENT  = 5,
        CTRL_SHUTDOWN_EVENT= 6,
    };
    typedef int __stdcall (*PHANDLER_ROUTINE)(unsigned long dwCtrlType);
    int __stdcall SetConsoleCtrlHandler(PHANDLER_ROUTINE HandlerRoutine, int Add);
    int _kbhit(void);
    int _getch(void);
    ]]
end
local _TTY = {}
local received_nb_signals = 0
local function sigterm_handler(sig)
    _TTY.sigterm = sig;
    received_nb_signals = received_nb_signals + 1
    --term_exit_sigsafe();
    if received_nb_signals > 3  then
        io.stderr:write('Received > 3 system signals, hard exiting\n')
        os.exit(123)
    end
    return true
end
local read_key
if jit.os == 'Windows' then
    ffi.C.SetConsoleCtrlHandler(ffi.cast("PHANDLER_ROUTINE", function (fdwCtrlType)
        FFmpeg.av_log(nil, FFmpeg.AV_LOG_DEBUG, "\nReceived windows signal %ld\n", fdwCtrlType);
        if fdwCtrlType == ffi.C.CTRL_C_EVENT or fdwCtrlType == ffi.C.CTRL_BREAK_EVENT then
            return sigterm_handler('SIGINT')
        elseif fdwCtrlType == ffi.C.CTRL_CLOSE_EVENT
            or fdwCtrlType == ffi.C.CTRL_LOGOFF_EVENT
            or fdwCtrlType == ffi.C.CTRL_SHUTDOWN_EVENT then
            return sigterm_handler('SIGTERM')
        else
            FFmpeg.av_log(nil, FFmpeg.AV_LOG_ERROR, "Received unknown windows signal %ld\n", fdwCtrlType);
            return false
        end
    end), true)
    read_key = function()
        if ffi.C._kbhit() ~= 0 then return string.char(ffi.C._getch()) end
    end
end
FFmpeg.av_log(nil, FFmpeg.AV_LOG_INFO, "Press [q] to stop, [?] for help\n")
local last_time = 0
_TTY.check = function (cur_time)
    local key
    if cur_time - last_time >= 100000  then
        key, last_time =  read_key(), cur_time
    end
    if _TTY.sigterm then return -1
    elseif not key then return 0
    elseif key == '?' then
        io.stderr:write[[
        key    function
        ?      show this help
        +      increase verbosity
        -      decrease verbosity
        c      Send command to first matching filter supporting it
        C      Send/Queue command to all matching filters
        D      cycle through available debug modes
        h      dump packets/hex press to cycle through the 3 states
        q      quit
        s      Show QP histogram
        ]]
    elseif key == 'q' then return -1
    elseif key == '+' then FFmpeg.av_log_set_level(FFmpeg.av_log_get_level()+10);
    elseif key == '-' then FFmpeg.av_log_set_level(FFmpeg.av_log_get_level()-10);
    --elseif key == 's' then qp_hist   ^= 1;
    end
    return 0
end
return _TTY
