#pragma once

#include "file_player_decoder.h"

namespace FilePlayer
{

class Player;
typedef std::shared_ptr<Player> PlayerPtr;

class PlayerListener
{
public:
    virtual ~PlayerListener() {}
    virtual void OnVideoFrame(int playedMs, int width, int height, void *data) = 0;
    virtual void OnAudioFrame(int playedMs, int samplingHz, int channels, void *buf, int len) = 0;
};

typedef std::shared_ptr<PlayerListener> PlayerListenerPtr;

class Player : public RecMutex
{
public:
    Player();
    ~Player();

    bool Open(const char* filePath, const PlayerListenerPtr& listener, bool audioPull);
    void Close();

    const char * GetInfo(const char *name, int* val);

    int RequestAudioOutput(int samplingHz, int channels, void *buf, int len);

    void Start();
    void Stop();
    bool Pause();
    bool Resume();
    bool Seek(int mode, int ms);

    void AudioSchd(int64_t elapse);
    void VideoSchd(int64_t elapse);

private:
    bool _audioPull;
    bool _pause;
    int64_t _playTs;
    
    DecoderPtr _decoder;
    PlayerListenerPtr _listener;
    MediaInfo _mediaInfo;

    int _audioIntervalMs;
    int _videoIntervalMs;

    SchedulerPtr _audioScheduler;
    SchedulerPtr _videoScheduler;

    AudioFramePtr _audioRemain;
    VideoFramePtr _videoRemain;
};

} // namespace FilePlayer

