#include "file_player_decoder.h"
#include <third_party/libyuv/include/libyuv.h> 
namespace FilePlayer
{

VideoFrame::VideoFrame(int64_t timestamp, int64_t len, int width, int height)
    : Queue::Item(timestamp, len)
    , _width(width)
    , _height(height)
{
    _frame = av_frame_alloc();
    if (_frame)
    {
        _buf = (uint8_t *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1));
        if (_buf)
            av_image_fill_arrays(_frame->data, _frame->linesize, (uint8_t *)_buf, AV_PIX_FMT_YUV420P, width, height, 1);
        else
            av_frame_free(&_frame);
    }

    UTIL_LOGFMT_DBG("Decoder", "VideoFrame %lld.", _ts);
}

VideoFrame::~VideoFrame()
{
    UTIL_LOGFMT_DBG("Decoder", "~VideoFrame %d.", _ts);
    if (_buf)
        av_free(_buf);
    if (_frame)
        av_frame_free(&_frame);
}

AudioFrame::AudioFrame(int64_t timestamp, int64_t len, int samples,
    uint64_t channelLayout, uint64_t sampleRate,
    enum AVSampleFormat sampleFormat)
    : Queue::Item(timestamp, len)
    , _samplingHz((int)sampleRate)
    , _samples(samples)
    , _offset(0)
{
    _channels = av_get_channel_layout_nb_channels(channelLayout);
    _len = av_samples_get_buffer_size(NULL, _channels, samples, sampleFormat, 0);
    _bytesPerChannel = _len / samples / _channels;
    
    _frame = av_frame_alloc();
    if (_frame)
    {
        _buf = av_malloc(_len);
        if (_buf)
            av_samples_fill_arrays(_frame->data, _frame->linesize, (uint8_t *)_buf, _channels, samples, sampleFormat, 0);
        else
            av_frame_free(&_frame);
    }

    UTIL_LOGFMT_DBG("Decoder", "AudioFrame %lld.", _ts);
}

AudioFrame::~AudioFrame()
{
    UTIL_LOGFMT_DBG("Decoder", "~AudioFrame %lld.", _ts);

    if (_buf)
        av_free(_buf);
    if (_frame)
        av_frame_free(&_frame);
}

Decoder::Decoder()
    : _id(0)
    , _formatCtx(NULL)
    , _info()
    , _videoFrames(new Queue())
    , _audioFrames(new Queue())
    , _videoConverter(NULL)
    , _audioConverter(NULL)
{
    for (int i = 0; i < MEDIA_TYPE_COUNT; i++)
        _streamIdxs[i] = -1;
}

Decoder::~Decoder()
{
    Close();

    //if (_videoConverter)
    //    sws_freeContext(_videoConverter);

    //if (_audioConverter)
    //    swr_free(&_audioConverter);
}

bool Decoder::Open(const char* filePath)
{
    LockGuard _(*this);

    if (_formatCtx)
    {
        UTIL_LOGFMT_ERR("Decoder", "Already Opened.");
        return false;
    }
    if (avformat_open_input(&_formatCtx, filePath, NULL, NULL) != 0)
        return false;
    if (avformat_find_stream_info(_formatCtx, NULL) < 0) {
        avformat_close_input(&_formatCtx);
        return false;
    }
    if (InitDecoder(AVMEDIA_TYPE_VIDEO))
        InitVideoInfo();

    if (InitDecoder(AVMEDIA_TYPE_AUDIO))
        InitAudioInfo();

    startRun();
    return true;
}

void Decoder::Close()
{
    {
        LockGuard _(*this);
        _videoFrames->Clear();
        _audioFrames->Clear();
    }
    stopRun();

    LockGuard _(*this);

    if (!_formatCtx)
        return;

    for (int i = 0; i < MEDIA_TYPE_COUNT; i++)
    {
        if (_streamIdxs[i] != -1)
        {
            avcodec_close(_formatCtx->streams[_streamIdxs[i]]->codec);
            _streamIdxs[i] = -1;
        }
    }
    avformat_close_input(&_formatCtx);
    avformat_free_context(_formatCtx);
    _formatCtx = NULL;
}

const MediaInfo& Decoder::MediaInfo()
{
    return _info;
}

bool Decoder::Read(VideoFramePtr& frame)
{
    if (_videoFrames->TsLength() == 0)
    {
        frame = 0;
        return false;
    }

    frame = std::static_pointer_cast<VideoFrame>(_videoFrames->Pop());
    return true;
}

bool Decoder::Read(AudioFramePtr& frame)
{
    if (_audioFrames->TsLength() == 0)
    {
        frame = 0;
        return false;
    }

    frame = std::static_pointer_cast<AudioFrame>(_audioFrames->Pop());
    return true;
}

bool Decoder::Seek(int toMs)
{
    LockGuard _(*this);

    int streamIdx = _streamIdxs[AVMEDIA_TYPE_VIDEO];
    if (streamIdx == -1)
        streamIdx = _streamIdxs[AVMEDIA_TYPE_AUDIO];
    if (streamIdx == -1)
        return false;

    AVStream *stream = _formatCtx->streams[streamIdx];
    int64_t ts = (int64_t)toMs * stream->time_base.den / (stream->time_base.num * 1000);

    if (av_seek_frame(_formatCtx, streamIdx, ts, AVSEEK_FLAG_BACKWARD) < 0)
    {
        UTIL_LOGFMT_ERR("Decoder", "Seek:%lld MS:%d", ts, toMs);
        return false;
    }

    UTIL_LOGFMT_IFO("Decoder", "Seek:%lld MS:%d", ts, toMs);

    _videoFrames->Clear();
    _audioFrames->Clear();

    ReinitDecoder(AVMEDIA_TYPE_AUDIO);
    ReinitDecoder(AVMEDIA_TYPE_VIDEO);

    return true;
}

bool Decoder::SetAudioConfig(int samplingHz, int channels, int bytesPerChannel)
{
    LockGuard _(*this);

    if (_info.Audio.SamplingHz == samplingHz
        && _info.Audio.ChannelsPerSample == channels
        && _info.Audio.BytesPerChannel == bytesPerChannel)
        return true;

    _info.Audio.SamplingHz = samplingHz;
    _info.Audio.ChannelsPerSample = channels;
    _info.Audio.BytesPerChannel = bytesPerChannel;

    if (_audioConverter)
    {
        //swr_free(&_audioConverter);
        _audioConverter = NULL;
    }

    return true;
}

void Decoder::onRun()
{
    AVFrame *frame = av_frame_alloc();

    AVPacket packet;
    av_init_packet(&packet);
    packet.data = NULL;
    packet.size = 0;

    bool video = _info.Video.Valid, audio = _info.Audio.Valid;

    while (isRunning())
    {
        {
            LockGuard _(*this);
            
            if (av_read_frame(_formatCtx, &packet) < 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            
            AVPacket originalPacket = packet;
            
            do
            {
                int ret = -1;
            
                if (packet.stream_index == _streamIdxs[AVMEDIA_TYPE_VIDEO])
                {
                    ret = DecodeVideoFrame(frame, &packet);
                }
                else if (packet.stream_index == _streamIdxs[AVMEDIA_TYPE_AUDIO])
                {
                    ret = DecodeAudioFrame(frame, &packet);
                }
            
                if (ret < 0)
                    break;
            
                packet.data += ret;
                packet.size -= ret;
            }
            while (packet.size > 0);
            
            av_packet_unref(&originalPacket);
        }
        
        while ((!video || _videoFrames->TsLength() > 100)
               && (!audio || _audioFrames->TsLength() > 100))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    av_frame_free(&frame);
}

bool Decoder::InitDecoder(enum AVMediaType type)
{
    if (type == AVMEDIA_TYPE_UNKNOWN || type > AVMEDIA_TYPE_AUDIO)
        return false;

    if (_streamIdxs[type] != -1)
    {
        UTIL_LOGFMT_ERR("Decoder", "Stream:%d Already Initialized", type);
        return false;
    }

    for (unsigned int i = 0; i < _formatCtx->nb_streams; i++)
    {
        if (_formatCtx->streams[i]->codec->codec_type == type)
        {
            _streamIdxs[type] = i;
            break;
        }
    }

    if (_streamIdxs[type] == -1)
    {
        UTIL_LOGFMT_IFO("Decoder", "Stream:%d Not Found", type);
        return false;
    }

    AVCodecContext *pCodecCtx = _formatCtx->streams[_streamIdxs[type]]->codec;
    AVCodec *pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if (pCodec == NULL)
    {
        UTIL_LOGFMT_ERR("Decoder", "Codec:%d Not Found", type);
        _streamIdxs[type] = -1;
        return false;
    }

    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
    {
        UTIL_LOGFMT_ERR("Decoder", "Codec:%d Open Failed.", type);
        _streamIdxs[type] = -1;
        return false;
    }

    UTIL_LOGFMT_IFO("Decoder", "Codec:%d Open.", type);
    return true;
}

void Decoder::ReinitDecoder(enum AVMediaType type)
{
    if (type == AVMEDIA_TYPE_UNKNOWN || type > AVMEDIA_TYPE_AUDIO)
        return;

    if (_streamIdxs[type] == -1)
        return;
    
    AVCodecContext *pCodecCtx = _formatCtx->streams[_streamIdxs[type]]->codec;
    avcodec_close(pCodecCtx);

    AVCodec *pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
    {
        UTIL_LOGFMT_ERR("Decoder", "Codec:%d ReOpen Failed.", type);
        _streamIdxs[type] = -1;
        return;
    }

    UTIL_LOGFMT_IFO("Decoder", "Codec:%d ReOpen.", type);
}

void Decoder::InitVideoInfo()
{
    if (_streamIdxs[AVMEDIA_TYPE_VIDEO] == -1)
        return;

    AVStream *pStream = _formatCtx->streams[_streamIdxs[AVMEDIA_TYPE_VIDEO]];
    AVCodecContext *pCodecCtx = pStream->codec;

    _info.Video.Valid = true;
    _info.Video.Width = pCodecCtx->width;
    _info.Video.Height = pCodecCtx->height;
    _info.Video.FrameRateFps = (double)pStream->avg_frame_rate.num / pStream->avg_frame_rate.den;
    _info.Video.LengthMs = (double)pStream->duration * 1000 * pStream->time_base.num / pStream->time_base.den;
}

void Decoder::InitAudioInfo()
{
    if (_streamIdxs[AVMEDIA_TYPE_AUDIO] == -1)
        return;
    
    //AVStream *pStream = _formatCtx->streams[_streamIdxs[AVMEDIA_TYPE_VIDEO]];
    //AVCodecContext *pCodecCtx = pStream->codec;

    _info.Audio.Valid = true;
    _info.Audio.SamplingHz = 48000;
    _info.Audio.ChannelsPerSample = 2;
    _info.Audio.BytesPerChannel = 2;
}

int Decoder::DecodeVideoFrame(AVFrame* frame, AVPacket* packet)
{
    if (_streamIdxs[AVMEDIA_TYPE_VIDEO] == -1)
        return packet->size;

    AVStream *stream = _formatCtx->streams[_streamIdxs[AVMEDIA_TYPE_VIDEO]];
    AVCodecContext *codecCtx = stream->codec;
    int gotFrame;

    int ret = avcodec_decode_video2(codecCtx, frame, &gotFrame, packet);
    if (ret < 0)
    {
        UTIL_LOGFMT_ERR("Decoder", "Decode Video Error.");
        return ret;
    }

    if (!gotFrame)
        return ret;

    //_videoConverter = sws_getCachedContext(_videoConverter,
    //    codecCtx->width, codecCtx->height, codecCtx->pix_fmt,
    //    _info.Video.Width, _info.Video.Height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

    frame->pts = av_frame_get_best_effort_timestamp(frame);
    int64_t ts = frame->pts * 1000 * stream->time_base.num / stream->time_base.den;
    int64_t len = 1000 / (int64_t)_info.Video.FrameRateFps;
    VideoFramePtr videoFrame = std::make_shared<VideoFrame>(ts, len, _info.Video.Width, _info.Video.Height);

    //if (sws_scale(_videoConverter, frame->data, frame->linesize, 0, codecCtx->height,
    //              videoFrame->_frame->data, videoFrame->_frame->linesize) > 0)
    //    _videoFrames->Push(videoFrame);
    if (0 == libyuv::I420Scale(
            frame->data[0], frame->linesize[0],
            frame->data[1], frame->linesize[1],
            frame->data[2], frame->linesize[2],
            codecCtx->width, codecCtx->height,
            videoFrame->_frame->data[0], videoFrame->_frame->linesize[0],
            videoFrame->_frame->data[1], videoFrame->_frame->linesize[1],
            videoFrame->_frame->data[2], videoFrame->_frame->linesize[2],
            _info.Video.Width, _info.Video.Height,
            libyuv::kFilterBox))
        _videoFrames->Push(videoFrame);

    if (codecCtx->refcounted_frames)
        av_frame_unref(frame);

    return ret;
}

int Decoder::DecodeAudioFrame(AVFrame* frame, AVPacket* packet)
{
    if (_streamIdxs[AVMEDIA_TYPE_AUDIO] == -1)
        return packet->size;
    
    AVStream *stream = _formatCtx->streams[_streamIdxs[AVMEDIA_TYPE_AUDIO]];
    AVCodecContext *codecCtx = stream->codec;
    int gotFrame;

    int ret = avcodec_decode_audio4(codecCtx, frame, &gotFrame, packet);
    if (ret < 0)
    {
        UTIL_LOGFMT_ERR("Decoder", "Decode Audio Error.");
        return ret;
    }

    ret = ret < packet->size ? ret : packet->size;

    if (!gotFrame)
        return ret;

    uint64_t channelLayout, sampleRate;
    enum AVSampleFormat sampleFormat;
    _audioConverter = GetSwrContext(_audioConverter, frame, channelLayout, sampleRate, sampleFormat);

    frame->pts = av_frame_get_best_effort_timestamp(frame);
    int64_t ts = frame->pts * 1000 * stream->time_base.num / stream->time_base.den;
    int64_t len = frame->nb_samples * 1000 / frame->sample_rate;
    size_t samples = (int)(frame->nb_samples * (sampleRate / frame->sample_rate + ((sampleRate % frame->sample_rate) ? 1 : 0)));
    AudioFramePtr audioFrame = std::make_shared<AudioFrame>(ts, len, samples, channelLayout, sampleRate, sampleFormat);

    //samples = swr_convert(_audioConverter, audioFrame->_frame->data, samples,
    //                     (const uint8_t **)frame->data, frame->nb_samples);
    if (0 == resampler_.Push((const int16_t *)frame->data, frame->nb_samples,
            (int16_t*)audioFrame->_frame->data, samples, samples))
    //if (samples > 0)
    {
        audioFrame->_samples = samples;
        audioFrame->_len = samples * audioFrame->_channels * audioFrame->_bytesPerChannel;
        _audioFrames->Push(audioFrame);
    }

    if (codecCtx->refcounted_frames)
        av_frame_unref(frame);
    
    return ret;
}

struct SwrContext * Decoder::GetSwrContext(struct SwrContext *context, AVFrame *frame,
    uint64_t& channelLayout, uint64_t& sampleRate, enum AVSampleFormat& sampleFormat)
{/*
    uint64_t sourceChannelLayout, sourceSampleRate;
    enum AVSampleFormat sourceSampleFormat;

    if (context)
    {
        av_opt_get_int(context, "in_channel_layout", 0, (int64_t *)&sourceChannelLayout);
        av_opt_get_int(context, "in_sample_rate", 0, (int64_t *)&sourceSampleRate);
        av_opt_get_sample_fmt(context, "in_sample_fmt", 0, &sourceSampleFormat);

        if (sourceChannelLayout != frame->channel_layout
            || sourceSampleRate != frame->sample_rate
            || sourceSampleFormat != frame->format)
        {
            swr_free(&context);
            context = NULL;
        }
    }
    else
    {
        sourceChannelLayout = frame->channel_layout;
        sourceSampleRate = frame->sample_rate;
        sourceSampleFormat = (enum AVSampleFormat)frame->format;
    }
*/
    sampleRate = _info.Audio.SamplingHz;
    sampleFormat = AV_SAMPLE_FMT_S16;
    if (_info.Audio.ChannelsPerSample == 1)
        channelLayout = AV_CH_LAYOUT_MONO;
    else
        channelLayout = AV_CH_LAYOUT_STEREO;
    resampler_.ResetIfNeeded(frame->sample_rate, _info.Audio.SamplingHz, _info.Audio.ChannelsPerSample);

/*
    if (context)
        return context;
    
    context = swr_alloc();
    if (!context)
    {
        UTIL_LOGFMT_ERR("Decoder", "Allocate Audio Converter.");
        return NULL;
    }

    av_opt_set_int(context, "in_channel_layout",    sourceChannelLayout, 0);
    av_opt_set_int(context, "in_sample_rate",       sourceSampleRate, 0);
    av_opt_set_sample_fmt(context, "in_sample_fmt", sourceSampleFormat, 0);

    av_opt_set_int(context, "out_channel_layout",    channelLayout, 0);
    av_opt_set_int(context, "out_sample_rate",       sampleRate, 0);
    av_opt_set_sample_fmt(context, "out_sample_fmt", sampleFormat, 0);
    
    if (swr_init(context) < 0)
    {
        UTIL_LOGFMT_ERR("Decoder", "Init Audio Converter.");
        swr_free(&context);
        return NULL;
    }
*/
    return context;
}


} // namespace FilePlayer

