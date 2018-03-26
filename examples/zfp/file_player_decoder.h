#pragma once
#include "file_player_queue.h"
#include <thread>
#include "webrtc/common_audio/resampler/include/resampler.h"
namespace FilePlayer
{

class VideoFrame : public Queue::Item
{
public:
    VideoFrame(int64_t timestamp, int64_t len, int width, int height);
    ~VideoFrame();

public:
    int _width;
    int _height;
    AVFrame *_frame;
    void *_buf;
};

typedef std::shared_ptr<VideoFrame> VideoFramePtr;

class AudioFrame : public Queue::Item
{
public:
    AudioFrame(int64_t timestamp, int64_t len, int samples,
        uint64_t channelLayout, uint64_t sampleRate,
        enum AVSampleFormat sampleFormat);
    ~AudioFrame();

public:
    int _samplingHz;
    int _channels;
    int _samples;
    int _bytesPerChannel;
    AVFrame *_frame;
    void *_buf;
    int _len;
    int _offset;
};

typedef std::shared_ptr<AudioFrame> AudioFramePtr;

struct VideoInfo
{
    VideoInfo() : Valid(false) {};
    
    bool Valid;
    int Width;
    int Height;
    double FrameRateFps;
    double LengthMs;
};

struct AudioInfo
{
    AudioInfo() : Valid(false) {};
    
    bool Valid;
    int SamplingHz;
    int BytesPerChannel;
    int ChannelsPerSample;
};

struct MediaInfo
{
    MediaInfo() : Video(), Audio() {};
    
    VideoInfo Video;
    AudioInfo Audio;
};
class Decoder : public RecMutex, public Thread
{
    #define MEDIA_TYPE_COUNT (AVMEDIA_TYPE_AUDIO + 1)
    
public:
    Decoder();
    ~Decoder();

    bool Open(const char* filePath);
    void Close();

    const MediaInfo& MediaInfo();

    bool Read(VideoFramePtr& frame);
    bool Read(AudioFramePtr& frame);
    bool Seek(int toMs);

    bool SetAudioConfig(int samplingHz, int channels, int bytesPerChannel);
    
    virtual void onRun() override;
private:
    bool InitDecoder(enum AVMediaType type);
    void ReinitDecoder(enum AVMediaType type);
    
    void InitVideoInfo();
    void InitAudioInfo();
    
    int DecodeVideoFrame(AVFrame* frame, AVPacket* packet);    
    int DecodeAudioFrame(AVFrame* frame, AVPacket* packet);

    struct SwrContext * GetSwrContext(struct SwrContext *context, AVFrame *frame,
        uint64_t& channelLayout, uint64_t& sampleRate, enum AVSampleFormat& sampleFormat);
        
private:
    int _id;
    AVFormatContext *_formatCtx;
    int _streamIdxs[MEDIA_TYPE_COUNT];

    struct MediaInfo _info;

    QueuePtr _videoFrames;
    QueuePtr _audioFrames;

    webrtc::Resampler resampler_;
    struct SwsContext *_videoConverter;
    struct SwrContext *_audioConverter;
};

typedef std::shared_ptr<Decoder> DecoderPtr;

} // namespace FilePlayer

