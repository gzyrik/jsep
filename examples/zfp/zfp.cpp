#include "file_player.h"
#include <map>
#include "zfp.h"
using namespace FilePlayer;
static PlayerPtr GetPlayer(const std::string& filePath, int mode=0){
    static std::map<std::string, PlayerPtr> players;
    static std::recursive_mutex mutex;
    std::lock_guard<std::recursive_mutex> _(mutex);
    auto iter = players.find(filePath);
    if (iter != players.end()){
        auto ret = iter->second;
        if (mode < 0) players.erase(iter);
        return ret;
    }
    else if (mode > 0)
        return players[filePath] = std::make_shared<Player>();
    return nullptr;
}
typedef void (*PFN_ZFP_ONVIDEO)(void *userdata, int playedMs, int width, int height, void *buf);
typedef void (*PFN_ZFP_ONAUDIO)(void *userdata, int playedMs, int samplingHz, int channels, void *buf, int len);
class ZfpPlayerListener : public PlayerListener
{
public:
    ZfpPlayerListener(void *userdata, PFN_ZFP_ONVIDEO pfnVideo, PFN_ZFP_ONAUDIO pfnAudio)
        : _userdata(userdata) , _pfnVideo(pfnVideo) , _pfnAudio(pfnAudio)
    {}
    virtual void OnVideoFrame(int playedMs, int width, int height, void *data) override {
        if (_pfnVideo) _pfnVideo(_userdata, playedMs, width, height, data);
    }
    virtual void OnAudioFrame(int playedMs, int samplingHz, int channels, void *buf, int len) override {
        if (_pfnAudio) _pfnAudio(_userdata, playedMs, samplingHz, channels, buf, len);
    }
private:
    void *_userdata;
    PFN_ZFP_ONVIDEO _pfnVideo;
    PFN_ZFP_ONAUDIO _pfnAudio;
};

int Zfp_Open(const char *filePath, void *userdata,
    void (*pfnVideo)(void *userdata, int playedMs, int width, int height, void *i420Buf),
    void (*pfnAudio)(void *userdata, int playedMs, int samplingHz, int channels, void *pcmBuf, int len)){
    auto player = GetPlayer(filePath, 1);
    if (!player->Open(filePath, std::make_shared<ZfpPlayerListener>(userdata, pfnVideo, pfnAudio), !pfnAudio)) {
        GetPlayer(filePath, -1);
        return -1;
    }
    return 0;
}
void Zfp_Close(const char *filePath){
    auto player = GetPlayer(filePath, -1);
    if (player) player->Close();
}
const char* Zfp_GetInfo(const char *filePath, const char *name, int* val){
    auto player = GetPlayer(filePath);
    if (player) return player->GetInfo(name, val);
    return 0;
}
int Zfp_ReadAudio(const char *filePath, int samplingHz, int channcels, void *pcmBuf, int len){
    auto player = GetPlayer(filePath);
    if (player) return player->RequestAudioOutput(samplingHz, channcels, pcmBuf, len);
    return -1;
}
int Zfp_Pause(const char *filePath) {
    auto player = GetPlayer(filePath);
    if (player && player->Pause()) return 0;
    return -1;
}
int Zfp_Resume(const char *filePath) {
    auto player = GetPlayer(filePath);
    if (player && player->Resume()) return 0;
    return -1;
}
int Zfp_Seek(const char *filePath, int offsetMs, int origin) {
    auto player = GetPlayer(filePath);
    if (player && player->Seek(origin, offsetMs)) return 0;
    return -1;
}
