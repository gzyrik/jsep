local ffi = require'ffi'
local bit = require'bit'
local FFmpeg = loadfile('init.lua')[[Z:\develop\ffmpeg-3.4.2-win64]]

local function encode(enc_ctx, frame, pkt, outfile)
    ret = FFmpeg.avcodec_send_frame(enc_ctx, frame);
    while ret >= 0 do
        ret = FFmpeg.avcodec_receive_packet(enc_ctx, pkt);
        if ret ~= 0 then break end

        print('Write packet '..tonumber(pkt.pts), ' size='..pkt.size)
        outfile:write(ffi.string(pkt.data, pkt.size))
        FFmpeg.av_packet_unref(pkt);
    end
end

local function main(filename)
    --find the mpeg1video encoder
    local codec = FFmpeg.avcodec_find_encoder_by_name('mpeg1video')
    local c = FFmpeg.avcodec_alloc_context3(codec);
    local pkt = FFmpeg.av_packet_alloc();

    -- put sample parameters
    c.bit_rate = 400000;
    -- resolution must be a multiple of two
    c.width = 352;
    c.height = 288;
    -- 25 frames per second */
    c.time_base.num = 1
    c.time_base.den = 25
    c.framerate.num = 25
    c.framerate.den = 1

    -- emit one intra frame every ten frames
    -- check frame pict_type before passing frame
    -- to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
    -- then gop_size is ignored and the output of encoder
    -- will always be I frame irrespective to gop_size
    c.gop_size = 10;
    c.max_b_frames = 1;
    c.pix_fmt = FFmpeg.AV_PIX_FMT_YUV420P

    if codec.id == FFmpeg.AV_CODEC_ID_H264 then
        FFmpeg.av_opt_set(c.priv_data, "preset", "slow", 0);
    end
    FFmpeg.avcodec_open2(c, codec, nil);
    local f = io.open(filename, "wb")

    frame = FFmpeg.av_frame_alloc();
    frame.format = c.pix_fmt;
    frame.width  = c.width;
    frame.height = c.height;

    FFmpeg.av_frame_get_buffer(frame, 32);


    for i=0,25-1 do
        io.flush()

        -- make sure the frame data is writable
        FFmpeg.av_frame_make_writable(frame);

        -- prepare a dummy image
        -- Y
        for y=0,c.height-1 do
            for x=0, c.width-1 do
                frame.data[0][y * frame.linesize[0] + x] = x + y + i * 3;
            end
        end

        -- Cb and Cr
        for y=0, c.height/2-1 do
            for x =0,c.width/2-1 do
                frame.data[1][y * frame.linesize[1] + x] = 128 + y + i * 2;
                frame.data[2][y * frame.linesize[2] + x] = 64 + x + i * 5;
            end
        end

        frame.pts = i;

        -- encode the image
        encode(c, frame, pkt, f);
    end

    -- flush the encoder
    encode(c, nil, pkt, f);

    -- add sequence end code to have a real MPEG file
    f:write(string.char(0,0,1,0xb7))
    f:close()

    FFmpeg.avcodec_free_context(ffi.new('AVCodecContext*[1]', c));
    FFmpeg.av_frame_free(ffi.new('AVFrame*[1]', frame));
    FFmpeg.av_packet_free(ffi.new('AVPacket*[1]', pkt));
end
FFmpeg.av_log_set_level(FFmpeg.AV_LOG_ERROR)
local ret, msg = pcall(main, '1.mpeg')
if not ret then print(msg) end
