local ffi = require'ffi'
local bit = require'bit'
local FFmpeg = loadfile('init.lua')[[Z:\Works\justalk\sdk\grape\cranberry\cranberry\external\FFmpeg\3.0.1-win32]]
-- 5 seconds stream duration
local STREAM_DURATION = 5.0  
local STREAM_FRAME_RATE =25 --25 images/s
local STREAM_NB_FRAMES = STREAM_DURATION * STREAM_FRAME_RATE
local STREAM_PIX_FMT= FFmpeg.AV_PIX_FMT_YUV420P -- default pix_fmt */  

-- add an audio output stream 
local function add_audio_stream(oc, codec_id) 
    local st =FFmpeg.avformat_new_stream(oc, nil)
    st.id = 1;
    codec_context = st.codec;
    codec_context.codec_id = codec_id;  
    codec_context.codec_type = FFmpeg.AVMEDIA_TYPE_AUDIO;  
  
    -- put sample parameters
    codec_context.sample_fmt = FFmpeg.AV_SAMPLE_FMT_S16;  
    codec_context.bit_rate = 64000;  
    codec_context.sample_rate = 44100;  
    codec_context.channels = 2;  
  
    --some formats want stream headers to be separate  
    if bit.band(oc.oformat.flags, FFmpeg.AVFMT_GLOBALHEADER) ~= 0 then
        codec_context.flags = bit.bor(codec_context.flags, FFmpeg.AV_CODEC_FLAG_GLOBAL_HEADER)
    end
    return st;  
end
--add a video output stream
local function add_video_stream(oc, codec_id)  
    local st = FFmpeg.avformat_new_stream(oc, nil)
    codec_context = st.codec
  
    -- find the video encoder
    local codec = FFmpeg.avcodec_find_encoder(codec_id);  

    FFmpeg.avcodec_get_context_defaults3(codec_context, codec);  
  
    codec_context.codec_id = codec_id;  
  
    -- put sample parameters
    codec_context.bit_rate = 3000000;  
    -- resolution must be a multiple of two
    codec_context.width = 640;  
    codec_context.height = 480;  
    -- time base: this is the fundamental unit of time (in seconds) in terms 
    -- of which frame timestamps are represented. for fixed-fps content, 
    -- timebase should be 1/framerate and timestamp increments should be 
    -- identically 1. 
    st.time_base.num = 1; 
    st.time_base.den = STREAM_FRAME_RATE;  
    codec_context.gop_size = 12; -- emit one intra frame every twelve frames at most
    codec_context.pix_fmt = STREAM_PIX_FMT;  
    if codec_context.codec_id == FFmpeg.AV_CODEC_ID_MPEG2VIDEO then
        -- just for testing, we also add B frames
        codec_context.max_b_frames = 2;  
    end
    if codec_context.codec_id == FFmpeg.AV_CODEC_ID_MPEG1VIDEO then
        -- Needed to avoid using macroblocks in which some coeffs overflow. 
        -- This does not happen with normal video, it just happens here as 
        -- the motion of the chroma plane does not match the luma plane.
        codec_context.mb_decision=2
    end
    -- some formats want stream headers to be separate  
    if bit.band(oc.oformat.flags, FFmpeg.AVFMT_GLOBALHEADER) ~= 0 then
        codec_context.flags = bit.bor(codec_context.flags, FFmpeg.AV_CODEC_FLAG_GLOBAL_HEADER)
    end
    return st;  
end
local function alloc_picture(pix_fmt, width, height)  
    local picture = FFmpeg.av_frame_alloc();
    local size = FFmpeg.avpicture_get_size(pix_fmt, width, height);  
    local picture_buf = FFmpeg.av_malloc(size);  
    picture.format = pix_fmt;
    picture.width = width;
    picture.height = height;
    FFmpeg.avpicture_fill(ffi.cast('struct AVPicture*',picture), picture_buf,  pix_fmt, width, height);  
    return picture;  
end
local audio_outbuf_size, audio_outbuf, samples, tincr,tincr2
local function open_audio(oc, st)  
    local codec_context = st.codec

    -- find the audio encoder
    local codec = FFmpeg.avcodec_find_encoder(codec_context.codec_id);  

    -- open it
    FFmpeg.avcodec_open2(codec_context, codec, nil)

    -- init signal generator
    t = 0; 
    tincr = 2 * math.pi * 110.0 / codec_context.sample_rate;  
    -- increment frequency by 110 Hz per second
    tincr2 = 2 * math.pi * 110.0 / codec_context.sample_rate / codec_context.sample_rate;  
  
    audio_outbuf_size = 10000;  
    audio_outbuf = FFmpeg.av_malloc(audio_outbuf_size);  
  
    -- ugly hack for PCM codecs (will be removed ASAP with new PCM 
    -- support to compute the input frame size in samples
    if codec_context.frame_size <= 1 then
        audio_input_frame_size = audio_outbuf_size / codec_context.channels;  
        if  codec_context.codec_id == FFmpeg.AV_CODEC_ID_PCM_S16LE or
            codec_context.codec_id == FFmpeg.AV_CODEC_ID_PCM_S16BE or
            codec_context.codec_id == FFmpeg.AV_CODEC_ID_PCM_U16LE or
            codec_context.codec_id == FFmpeg.AV_CODEC_ID_PCM_U16BE then
            audio_input_frame_size = bit.rshift(audio_input_frame_size, 1)  
        end
    else
        audio_input_frame_size = codec_context.frame_size;  
    end
    samples = FFmpeg.av_malloc(audio_input_frame_size * 2 * codec_context.channels);
end
local video_outbuf, picture, tmp_picture
local frame_count = 0
local function open_video(oc, st)  
    local codec_context = st.codec;
  
    -- find the video encoder
    local codec = FFmpeg.avcodec_find_encoder(codec_context.codec_id);

    -- open the codec
    FFmpeg.avcodec_open2(codec_context, codec, nil)

    if bit.band(oc.oformat.flags, FFmpeg.AVFMT_RAWPICTURE) == 0  then  
        -- allocate output buffer
        -- XXX: API change will be done
        -- buffers passed into lav* can be allocated any way you prefer, 
        -- as long as they're aligned enough for the architecture, and 
        -- they're freed appropriately (such as using av_free for buffers 
        -- allocated with av_malloc)
        video_outbuf_size = 200000;  
        video_outbuf = FFmpeg.av_malloc(video_outbuf_size);  
    end
  
    -- allocate the encoded raw picture
    picture = alloc_picture(codec_context.pix_fmt, codec_context.width, codec_context.height);  
  
    -- if the output format is not YUV420P, then a temporary YUV420P 
    -- picture is needed too. It is then converted to the required 
    -- output format
    if codec_context.pix_fmt ~= FFmpeg.AV_PIX_FMT_YUV420P then
        tmp_picture = alloc_picture(FFmpeg.AV_PIX_FMT_YUV420P, codec_context.width, codec_context.height);  
    end
end
local function close_audio(oc, st)  
    FFmpeg.avcodec_close(st.codec);  
    FFmpeg.av_free(samples);  
    FFmpeg.av_free(audio_outbuf);  
end
local function close_video(oc, st)  
    FFmpeg.avcodec_close(st.codec);  
    FFmpeg.av_free(picture.data[0]);  
    FFmpeg.av_free(picture);  
    if tmp_picture then
        FFmpeg.av_free(tmp_picture.data[0]);  
        FFmpeg.av_free(tmp_picture);  
    end
    FFmpeg.av_free(video_outbuf)
end
-- prepare a 16 bit dummy audio frame of 'frame_size' samples and 
-- 'nb_channels' channels
local function get_audio_frame(samples, frame_size, nb_channels)  
    local q = 0  
    for j = 1, frame_size do
        local v = math.sin(t) * 10000  
        for i=1,nb_channels do
            samples[q] = v
            q = q+1
        end
        t = t + tincr;  
        tincr = tincr + tincr2;  
    end
end
local function write_audio_frame(oc, st)  
    local AVPacket ppkt = ffi.new('AVPacket[1]')
    FFmpeg.av_init_packet(ppkt)
    local pkt = ppkt[0]
  
    local codec_context = st.codec;  
  
    get_audio_frame(samples, audio_input_frame_size, codec_context.channels);  
  
    pkt.size = avcodec_encode_audio(codec_context, audio_outbuf, audio_outbuf_size, samples);  
  
    if codec_context.coded_frame and codec_context.coded_frame.pts ~= AV_NOPTS_VALUE then
        pkt.pts= av_rescale_q(codec_context.coded_frame.pts, codec_context.time_base, st.time_base);  
    end
    pkt.flags = bit.bor(pkt.flags, FFmpeg.AV_PKT_FLAG_KEY)
    pkt.stream_index = st.index;  
    pkt.data = audio_outbuf;  
  
    -- write the compressed frame in the media file
    FFmpeg.av_interleaved_write_frame(oc, ppkt)
end
-- prepare a dummy image
local function fill_yuv_image(pict, frame_index, width, height)  
    local i = frame_index;  
  
    -- Y
    for y=0,height-1 do
        for x=0,width-1 do
            pict.data[0][y * pict.linesize[0] + x] = x + y + i * 3;  
        end
    end
  
    --Cb and Cr
    for y=0, height/2-1 do
        for x=0, width/2-1 do
            pict.data[1][y * pict.linesize[1] + x] = 128 + y + i * 2;  
            pict.data[2][y * pict.linesize[2] + x] = 64 + x + i * 5;  
        end
    end
end
local function write_video_frame(oc, st)  
    local codec_context = st.codec;
  
    if frame_count >= STREAM_NB_FRAMES then
        -- no more frame to compress. The codec has a latency of a few 
        -- frames if using B frames, so we get the last frames by 
        -- passing the same picture again
    else
        if codec_context.pix_fmt ~= FFmpeg.AV_PIX_FMT_YUV420P then
            -- as we only generate a YUV420P picture, we must convert it 
            -- to the codec pixel format if needed
            if not img_convert_ctx then
                img_convert_ctx = FFmpeg.sws_getContext(codec_context.width, codec_context.height,  
                    FFmpeg.AV_PIX_FMT_YUV420P,  
                    codec_context.width, codec_context.height,  
                    codec_context.pix_fmt,  
                    sws_flags, nil, nil, nil);  
            end
            fill_yuv_image(tmp_picture, frame_count, codec_context.width, codec_context.height);  
            sws_scale(img_convert_ctx, tmp_picture.data, tmp_picture.linesize,  
            0, codec_context.height, picture.data, picture.linesize);  
        else
            fill_yuv_image(picture, frame_count, codec_context.width, codec_context.height);  
        end
    end
  
    if 0 ~= bit.band(oc.oformat.flags, FFmpeg.AVFMT_RAWPICTURE) then 
        -- raw video case. The API will change slightly in the near 
        -- future for that.
        local AVPacket ppkt = ffi.new('AVPacket[1]')
        FFmpeg.av_init_packet(ppkt)
        local pkt = ppkt[0]

        pkt.flags = bit.bor(pkt.flags, FFmpeg.AV_PKT_FLAG_KEY)
        pkt.stream_index = st.index;  
        pkt.data = picture;  
        pkt.size = ffi.sizeof('AVPicture');  
  
        ret = FFmpeg.av_interleaved_write_frame(oc, ppkt);  
    else
        local AVPacket ppkt = ffi.new('AVPacket[1]')
        FFmpeg.av_init_packet(ppkt)
        local pkt = ppkt[0]
        pkt.stream_index = st.index;  
        pkt.data = video_outbuf;  
        pkt.size = video_outbuf_size;
        local got_packet_ptr = ffi.new('int[1]')
        -- encode the image
        FFmpeg.avcodec_encode_video2(codec_context, ppkt, picture, got_packet_ptr);  
        -- if zero size, it means the image was buffered
        if got_packet_ptr[0] == 1 then
            if codec_context.coded_frame.pts ~= FFmpeg.AV_NOPTS_VALUE  then 
                pkt.pts= av_rescale_q(codec_context.coded_frame.pts, codec_context.time_base, st.time_base);  
            end
            if codec_context.coded_frame.key_frame then
                pkt.flags = bit.bor(pkt.flags, FFmpeg.AV_PKT_FLAG_KEY);
            end
  
            -- write the compressed frame in the media file
            ret = FFmpeg.av_interleaved_write_frame(oc, ppkt);  
        end
    end
    frame_count = frame_count + 1
end
function main(filename)
    local _oc = ffi.new('AVFormatContext*[1]')

    --allocate the output media context
    FFmpeg.avformat_alloc_output_context2(_oc, nil, nil, filename)
    local oc = _oc[0]

    -- 强制指定 264 编码
    oc.oformat.video_codec = FFmpeg.AV_CODEC_ID_H264
    oc.oformat.audio_codec = FFmpeg.AV_CODEC_ID_PCM_S16LE --FFmpeg.AV_CODEC_ID_AAC

    --add the audio and video streams using the default format codecs 
    --and initialize the codecs
    local video_st, audio_st
    if oc.oformat.video_codec ~= FFmpeg.AV_CODEC_ID_NONE then
        video_st = add_video_stream(oc, oc.oformat.video_codec);  
    end
    if oc.oformat.audio_codec ~= FFmpeg.AV_CODEC_ID_NONE then
        audio_st = add_audio_stream(oc, oc.oformat.audio_codec);  
    end
    FFmpeg.av_dump_format(oc, 0, filename, 1); 

    --now that all the parameters are set, we can open the audio and 
    --video codecs and allocate the necessary encode buffers 
    if video_st then open_video(oc, video_st) end
    if audio_st then open_audio(oc, audio_st) end

    --open the output file, if needed
    local isfile = bit.band(oc.oformat.flags, FFmpeg.AVFMT_NOFILE) ==0
    if isfile then
        local _pb = ffi.new('AVIOContext*[1]')
        FFmpeg.avio_open(_pb, filename, FFmpeg.AVIO_FLAG_WRITE)
        oc.pb = _pb[0]
    end

    --write the stream header, if any
    FFmpeg.avformat_write_header(oc, nil)
    picture.pts = 0
    while picture.pts < STREAM_NB_FRAMES do
        -- compute current audio and video time
        if audio_st then
            audio_pts = audio_st.pts.val * audio_st.time_base.num / audio_st.time_base.den;  
        else  
            audio_pts = 0.0
        end
  
        if video_st then
            video_pts = video_st.pts.val * video_st.time_base.num / video_st.time_base.den;  
        else  
            video_pts = 0.0;  
        end
  
        if (not audio_st or audio_pts >= STREAM_DURATION)
            and (not video_st or video_pts >= STREAM_DURATION) then
            break;  
        end
  
        -- write interleaved audio and video frames
        if not video_st or (video_st and audio_st and audio_pts < video_pts) then 
            write_audio_frame(oc, audio_st);  
        else
            write_video_frame(oc, video_st);  
            picture.pts = picture.pts + 1  
        end
    end

    --write the trailer, if any.  the trailer must be written 
    -- before you close the CodecContexts open when you wrote the 
    -- header; otherwise write_trailer may try to use memory that 
    -- was freed on av_codec_close()
    FFmpeg.av_write_trailer(oc)

    --close each codec
    if video_st then close_video(oc, video_st) end
    if audio_st then close_audio(oc, audio_st) end

    --close the output file
    if isfile then FFmpeg.avio_close(oc.pb) end
  
    -- free the stream
    FFmpeg.avformat_free_context(oc);
end
local ret, msg = pcall(main, '1.avi')
if not ret then print(msg) end
