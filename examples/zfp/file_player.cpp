#include "file_player.h"
#include <cctype>
#include <algorithm>
namespace FilePlayer
{

class PlayerSchedulerAudio : public Scheduler::Listener
{
public:
    PlayerSchedulerAudio(Player* player)
        : _player(player)
    {
    }

    virtual void OnSchd(int64_t elapse) override
    {
        _player->AudioSchd(elapse);
    }

private:
    Player* _player;
};

class PlayerSchedulerVideo : public Scheduler::Listener
{
public:
    PlayerSchedulerVideo(Player* player)
        : _player(player)
    {
    }

    virtual void OnSchd(int64_t elapse) override
    {
        _player->VideoSchd(elapse);
    }

private:
    Player* _player;
};

Player::Player()
    : _audioPull(false)
    , _pause(false)
    , _playTs(-1)
    , _decoder(new Decoder())
    , _listener(0)
    , _mediaInfo()
    , _audioScheduler(0)
    , _videoScheduler(0)
    , _audioRemain(0)
{

}

Player::~Player()
{
    _decoder = 0;
    _audioScheduler = 0;
    _videoScheduler = 0;
    _audioRemain = 0;
}

bool Player::Open(const char* filePath, const PlayerListenerPtr& listener,
    bool audioPull)
{
    LockGuard _(*this);

    if (!_decoder->Open(filePath))
    {
        UTIL_LOGFMT_ERR("Player", "Open:%s Failed.", filePath);
        return false;
    }

    _listener = listener;
    _audioPull = audioPull;

    _mediaInfo = _decoder->MediaInfo();

    UTIL_LOGFMT_IFO("Player", "Open:%s", filePath);
    return true;
}

void Player::Close()
{
    UTIL_LOGFMT_IFO("Player", "Close", "");

    LockGuard _(*this);
    Stop();
    _decoder->Close();
    _listener = 0;
}

const char* Player::GetInfo(const char *name, int* val)
{
    std::string key(name);
    std::transform(key.begin(), key.end(), key.begin(), tolower);
    LockGuard _(*this);

    if (key == "lengthms")
    {
        if (_mediaInfo.Video.Valid && val){
            *val = (int)_mediaInfo.Video.LengthMs;
            return "";
        }
    }
    else if (key == "playedms")
    {
        if (val) {
            *val = (int)_playTs;
            return "";
        }
    }

    return 0;
}

int Player::RequestAudioOutput(int samplingHz, int channels,
    void *buf, int len)
{
    LockGuard _(*this);

    if (_pause)
    {
        memset(buf, 0, len);
        return len;
    }

    AudioFramePtr frame = 0;
    
    if (_audioRemain)
    {
        frame = _audioRemain;
        _audioRemain = 0;
    }
    else
    {
        _decoder->Read(frame);
    }

    if (!frame || frame->_samplingHz != samplingHz
        || frame->_channels != channels || frame->_bytesPerChannel != 2)
    {
        _decoder->SetAudioConfig(samplingHz, channels, 2);
        return 0;
    }

    int procLen, fill = 0;

    while (len)
    {
        if (!frame && !_decoder->Read(frame))
            break;

        procLen = frame->_len - frame->_offset;
        if (procLen > len)
            procLen = len;

        memcpy((char *)buf + fill, (void *)((char *)frame->_buf + frame->_offset), procLen);

        fill += procLen;
        len -= procLen;

        if (_playTs < 0)
            _playTs = 0;
        
        frame->_offset += procLen;
        if (frame->_offset == frame->_len)
        {
            _playTs = frame->_ts;
            frame = 0;
        }
    }

    if (frame)
        _audioRemain = frame;

    UTIL_LOGFMT_DBG("Player", "Audio:%lld Played", _playTs);
    return fill;
}

void Player::Start()
{
    UTIL_LOGFMT_IFO("Player", "Start", "");

    LockGuard _(*this);

    if (_videoScheduler || _audioScheduler)
        return;

    _pause = false;

    if (_mediaInfo.Video.Valid)
    {
        _videoScheduler = std::make_shared<Scheduler>();
        _videoScheduler->Start(10, std::make_shared<PlayerSchedulerVideo>(this));
    }

    if (_mediaInfo.Audio.Valid && !_audioPull)
    {
        _audioScheduler = std::make_shared<Scheduler>();
        _audioScheduler->Start(10, std::make_shared<PlayerSchedulerAudio>(this));
    }
}

void Player::Stop()
{
    UTIL_LOGFMT_IFO("Player", "Stop", "");

    LockGuard _(*this);

    _videoScheduler->Stop();
    if (!_audioPull) _audioScheduler->Stop();
}

bool Player::Pause()
{
    UTIL_LOGFMT_IFO("Player", "Pause", "");

    LockGuard _(*this);
    if (_videoScheduler || _audioScheduler) {
        _pause = true;
        _videoScheduler->Pause(true);
        if (!_audioPull) _audioScheduler->Pause(true);
    }
    return true;
}

bool Player::Resume()
{
    UTIL_LOGFMT_IFO("Player", "Resume", "");

    LockGuard _(*this);
    if (_videoScheduler || _audioScheduler){
        _pause = false;
        _videoScheduler->Pause(false);
        if (!_audioPull) _audioScheduler->Pause(false);
    }
    else
       Start();
    return true;
}

bool Player::Seek(int mode, int ms)
{
    UTIL_LOGFMT_IFO("Player", "Seek:%d Mode:%d", ms, mode);

    int len = (_mediaInfo.Video.Valid ? (int)_mediaInfo.Video.LengthMs : -1);
    if (len <= 0)
        return false;

    switch (mode)
    {
    case SEEK_SET:
        break;
    case SEEK_CUR:
        ms += (int)_playTs;
        break;
    case SEEK_END:
        ms += len;
        break;
    default:
        return false;
    }

    LockGuard _(*this);

    if (_mediaInfo.Video.Valid)
        _videoScheduler->Pause(true);
    if (_mediaInfo.Audio.Valid && !_audioPull)
        _audioScheduler->Pause(true);

    if (ms < 0) ms = 0;
    else if (ms > len) ms = len;

    if (!_decoder->Seek(ms))
        return false;

    if (!_pause)
    {
        if (_mediaInfo.Video.Valid)
            _videoScheduler->Pause(false);
        if (_mediaInfo.Audio.Valid && !_audioPull)
            _audioScheduler->Pause(false);
    }
    
    _playTs = -1;
    
    return true;
}

void Player::AudioSchd(int64_t elapse)
{
    PlayerListenerPtr listener = 0;
    AudioFramePtr frame = 0;
    int64_t playTs;
    
    lock();
        
    if (_audioRemain)
    {
        frame = _audioRemain;
        _audioRemain = 0;
    }
    else
    {
        _decoder->Read(frame);
    }

    listener = _listener;
    playTs = _playTs;

    if (!frame || frame->_ts > playTs + 200)
    {
        unlock();
        return;
    }

    int requestLen = frame->_samplingHz * (int)elapse * frame->_channels * frame->_bytesPerChannel / 1000;
    int procLen;

    while (requestLen)
    {
        if (!frame && !_decoder->Read(frame))
            break;

        procLen = frame->_len - frame->_offset;
        if (procLen > requestLen)
            procLen = requestLen;

        unlock();
        if (listener)
        {
            listener->OnAudioFrame((int)playTs, frame->_samplingHz, frame->_channels,
                (void *)((char *)frame->_buf + frame->_offset), procLen);
        }
        lock();

        requestLen -= procLen;        
        frame->_offset += procLen;
        if (frame->_offset == frame->_len)
            frame = 0;
    }

    if (frame && frame->_ts < playTs + 200)
        _audioRemain = frame;

    unlock();
}

void Player::VideoSchd(int64_t elapse)
{
    PlayerListenerPtr listener = 0;
    VideoFramePtr frame = 0;
    int64_t playTs;

    lock();

    if (_videoRemain)
    {
        frame = _videoRemain;
        _videoRemain = 0;
    }
    else
    {
        _decoder->Read(frame);
    }

    listener = _listener;
    playTs = _playTs;

    if (!frame || frame->_ts > playTs + 200)
    {
        unlock();
        return;
    }

    while (frame->_ts <= playTs)
    {
        UTIL_LOGFMT_DBG("Player", "Video:%lld", frame->_ts);

        unlock();

        if (listener)
            listener->OnVideoFrame((int)playTs, frame->_width, frame->_height, frame->_buf);

        lock();
        
        if (!_decoder->Read(frame))
            break;
    }

    if (frame && frame->_ts < playTs + 200)
    {
        _videoRemain = frame;
        UTIL_LOGFMT_DBG("Player", "Video:%lld remain", frame->_ts);
    }

    unlock();
}

} // namespace FilePlayer

