local ffi = require'ffi'
local bit = require'bit'
local FFmpeg = loadfile('init.lua')[[Z:\develop\ffmpeg-3.4.2-win64]]

local function open_render(i_stm, format_name)
    local fmt_ctx = ffi.new('AVFormatContext*[1]')
    local ret = FFmpeg.avformat_alloc_output_context2(fmt_ctx, nil, format_name, nil)
    FFmpeg.assert(ret, 'avformat_alloc_output_context2')
    local stream = FFmpeg.avformat_new_stream(fmt_ctx[0], nil)
    stream.time_base.num = 1
    stream.time_base.den = 25
    local codecpar = stream.codecpar

    codecpar.codec_type = i_stm.codecpar.codec_type;  
    codecpar.codec_id = i_stm.codecpar.codec_id;  
    codecpar.width = i_stm.codecpar.width;  
    codecpar.height = i_stm.codecpar.height;
    codecpar.format = FFmpeg.AV_PIX_FMT_YUV420P;

    --创建编码的AVCodecContext
    local codec = FFmpeg.avcodec_find_encoder(codecpar.codec_id)
    local codec_cxt = FFmpeg.avcodec_alloc_context3(codec);
    local ret = FFmpeg.avcodec_parameters_to_context(codec_cxt, codecpar)
    codec_cxt.time_base = stream.time_base
    FFmpeg.assert(ret, 'avcodec_parameters_to_context')
    local ret = FFmpeg.avcodec_open2(codec_cxt, codec, nil)
    FFmpeg.assert(ret, 'avcodec_open2')

    return fmt_ctx, stream, codec_cxt
end
local function open_video(url, format, options)
    if format then
        local fmt = FFmpeg.av_find_input_format(format)
        FFmpeg.assert(fmt, format)
        format = fmt
    end
    local fmt_ctx = ffi.new('AVFormatContext*[1]')
    local ret = FFmpeg.avformat_open_input(fmt_ctx, url, format, options)
    FFmpeg.assert(ret, 'avformat_open_input')

    local ret = FFmpeg.avformat_find_stream_info(fmt_ctx[0], nil)
    FFmpeg.assert(ret, 'avformat_find_stream_info')
    --输出调试信息:
    FFmpeg.av_dump_format(fmt_ctx[0], 0, url, 0);
    print[[
    tbr代表帧率
    tbn代表文件层（st）的时间精度，即1S=1200k，和duration相关
    tbc代表视频层（st->codec）的时间精度，即1S=XX，和stream->duration和时间戳相关)
    ]]
    --寻找视频流，有麦克风的摄像头同时还有音频流输入
    local videoStreamIndex
    for i=0, fmt_ctx[0].nb_streams-1 do
        if fmt_ctx[0].streams[i].codec.codec_type == FFmpeg.AVMEDIA_TYPE_VIDEO then
            videoStreamIndex = i
            break
        end
    end
    assert(videoStreamIndex)

    local stream = fmt_ctx[0].streams[videoStreamIndex]
    local codecpar = stream.codecpar

    print ('输入视频格式:', ffi.string(FFmpeg.avcodec_get_name(codecpar.codec_id)))
    print ('输入视频高度:', codecpar.height)
    print ('输入视频宽度:', codecpar.width)
    --获取帧率信息，注意输入流里面用r_frame_rate不用avg_frame_rate
    print ('输入视频帧率:', stream.r_frame_rate.num * 1.0 /  stream.r_frame_rate.den)

    --创建解码的AVCodecContext
    local codec = FFmpeg.avcodec_find_decoder(codecpar.codec_id)
    local codec_cxt = FFmpeg.avcodec_alloc_context3(codec);
    local ret = FFmpeg.avcodec_parameters_to_context(codec_cxt, codecpar)
    FFmpeg.assert(ret, 'avcodec_parameters_to_context')
    --codec_cxt.pix_fmt = FFmpeg.AV_PIX_FMT_YUV420P
    local ret = FFmpeg.avcodec_open2(codec_cxt, codec, nil)
    FFmpeg.assert(ret, 'avcodec_open2')

    return fmt_ctx, stream, codec_cxt
end
----------------------------------------------------------------------------------
local i_ctx, i_stm, decoder= open_video(arg[1] or 'video=FaceTime HD Camera', 'dshow')
local o_ctx, o_stm, encoder= open_render(i_stm, 'sdl')
----------------------------------------------------------------------------------
local i_par, o_par = i_stm.codecpar,o_stm.codecpar
local s_ctx = FFmpeg.sws_getCachedContext(nil,
i_par.width, i_par.height, i_par.format,
o_par.width, o_par.height, o_par.format,
FFmpeg.SWS_BICUBIC, nil, nil, nil)
----------------------------------------------------------------------------------
local options = ffi.new('AVDictionary*[1]')
FFmpeg.av_dict_set(options, 'window_title', 'dshow-sdl', 0);
FFmpeg.av_dict_set(options, 'window_size', 'cif', 0);
local ret = FFmpeg.avformat_write_header(o_ctx[0], options)
----------------------------------------------------------------------------------
local frame = FFmpeg.av_frame_alloc();
local packet = FFmpeg.av_packet_alloc();
local frame2 = FFmpeg.av_frame_alloc();
frame2.format = o_par.format;
frame2.width  = o_par.width;
frame2.height = o_par.height;
FFmpeg.av_frame_get_buffer(frame2, 32);

for i=0,10 do
    FFmpeg.av_read_frame(i_ctx[0], packet)
    ret = FFmpeg.avcodec_send_packet(decoder, packet);
    while ret >= 0 do
        ret = FFmpeg.avcodec_receive_frame(decoder, frame);
        FFmpeg.sws_scale(s_ctx, ffi.cast('const unsigned char *const *', frame.data), frame.linesize, 0, i_par.height, frame2.data, frame2.linesize)
        ret = FFmpeg.avcodec_send_frame(encoder, frame2);
        while ret >= 0 do
            ret = FFmpeg.avcodec_receive_packet(encoder, packet);
            ret = FFmpeg.av_write_frame(o_ctx[0], packet);
            --FFmpeg.assert(ret, 'render')
        end
        FFmpeg.av_frame_unref(frame);
    end
    FFmpeg.av_packet_unref(packet)
end
----------------------------------------------------------------------------------
print('clean')
FFmpeg.av_frame_free(ffi.new('AVFrame*[1]', frame));
FFmpeg.av_frame_free(ffi.new('AVFrame*[1]', frame2));
FFmpeg.av_packet_free(ffi.new('AVPacket*[1]', packet));
FFmpeg.avformat_close_input(i_ct);
FFmpeg.avformat_close_input(o_ctx);
----------------------------------------------------------------------------------


--[[
//----------从摄像头中获取帧----------
AVPacket packet;
int gotPicture;

AVFrame *pFrame = av_frame_alloc();  //注意av_frame_alloc并未实际分配空间

while( av_read_frame(pFormatCtx, &packet) >= 0 )
{
    if( packet.stream_index == videoStreamIndex )
    {
        //返回的pFrame由decoder管理
        avcodec_decode_video2(pCodecCtx, pFrame, &gotPicture, &packet); 

        //Do something

        if(gotPicture)
        {
            av_frame_unref(pFrame);
        }
    }

    av_free_packet(&packet);
}

//----------清理----------
if ( NULL != pCodecCtx )
{
    avcodec_close(pCodecCtx);
    pCodecCtx = NULL;
}

if ( NULL != pFormatCtx )
{
    avformat_close_input(&pFormatCtx);
    pFormatCtx = NULL;
}
]]
