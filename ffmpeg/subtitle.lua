local ffi = require'ffi'
local bit = require'bit'
local FFmpeg = loadfile('init.lua')[[Z:\develop\ffmpeg-3.4.2-win64]]

local filename = arg[1] or 'centaur_1.mpg'
local function save_subtitle(context, index)
    local filename = 'subtitle_'..index..'.ass'

    local avstream = context.streams[index];
    local codec = FFmpeg.avcodec_find_decoder(avstream.codec.codec_id);

    local result = FFmpeg.avcodec_open2(avstream.codec, codec, nil);
    FFmpeg.assert(result, 'avcodec_open2')

    local outFormat = FFmpeg.av_guess_format(nil, filename, nil);
    FFmpeg.assert(outFormat, filename)

    local outFormatContext_ = ffi.new('AVFormatContext*[1]')
    result = FFmpeg.avformat_alloc_output_context2(outFormatContext_, NULL, NULL, filename);
    FFmpeg.assert(result, filename)

    local outFormatContext = outFormatContext_[0]
    local encoder = FFmpeg.avcodec_find_encoder(outFormat.subtitle_codec);
    FFmpeg.assert(encoder, outFormat.subtitle_codec);

    local outStream = FFmpeg.avformat_new_stream(outFormatContext, encoder);
    FFmpeg.assert(outStream, "new_stream");

    local c = outStream.codec;
    result = FFmpeg.avcodec_get_context_defaults3(c, encoder);
    FFmpeg.assert(result, "get codec context");

    local pb = ffi.new('AVIOContext*[1]')
    result = FFmpeg.avio_open(pb, filename, FFmpeg.AVIO_FLAG_WRITE);
    FFmpeg.assert(result, "avio_open");

    outFormatContext.pb = pb[0]
    result = FFmpeg.avformat_write_header(outFormatContext, nil);
    FFmpeg.assert(result, "writing header");

    local pkt = FFmpeg.av_packet_alloc();
    local gotSubtitle = ffi.new('int[1]')
    local subtitle = ffi.new('AVSubtitle')
    while FFmpeg.av_read_frame(context, pkt) >= 0 do
        if pkt.stream_index == index then
            gotSubtitle[0] = 0
            result = FFmpeg.avcodec_decode_subtitle2(avstream.codec, subtitle, gotSubtitle, pkt);
            if  gotSubtitle[0] ~= 0 then
                local bufferSize = 1024 * 1024;
                local buffer = ffi.new('uint8_t[?]', bufferSize)
                result = FFmpeg.avcodec_encode_subtitle(outStream.codec, buffer, bufferSize, subtitle);
                print ("Encode subtitle result: " .. result)
                print ("Encoded subtitle: " .. ffi.string(buffer))
            end
        end
    end
    FFmpeg.av_packet_free(ffi.new('AVPacket*[1]', pkt));
end

local fmt_ctx = ffi.new('AVFormatContext*[1]')
local ret = FFmpeg.avformat_open_input(fmt_ctx, filename, nil, nil)
FFmpeg.assert(ret, filename)
save_subtitle(fmt_ctx[0], 0)
FFmpeg.avformat_close_input(fmt_ctx);
