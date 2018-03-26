// file_player_dll.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "file_player_dll.h"
#include "file_player_manager.h"

using namespace FilePlayer;

static PFN_ZFP_LOG m_pfnZfpLogCb = NULL;

void Zfp_LogCb(int level,const char *key,const std::string& info)
{
    //Common::String time =  Common::getTimeStr("%d-%02d-%02d %02d:%02d:%02d:%03d", Common::getCurTimeMs());
    //Common::String message;
    //message.format("%s LEVEL:%d KEY:%s %s\n", time.c_str(), level, key, info.c_str());
    if (m_pfnZfpLogCb)
        m_pfnZfpLogCb(info.c_str());
}

FILE_PLAYER_DLL_API bool Zfp_SetLogCallback(PFN_ZFP_LOG pfnLog)
{
    //if (pfnLog)
    //    Common::setLogCallback((Common::fn_LogCallback)Zfp_LogCb);
    //else
    //    Common::setLogCallback(NULL);
    m_pfnZfpLogCb = pfnLog;

    return true;
}

FILE_PLAYER_DLL_API int Zfp_Open(const char *filePath, ST_ZFP_CONFIG *pstConfig)
{
    class ZfpPlayerListener : public PlayerListener
    {
    public:
        ZfpPlayerListener(void *userdata,
            PFN_ZFP_ONVIDEO pfnVideo, PFN_ZFP_ONAUDIO pfnAudio)
            : _userdata(userdata)
            , _pfnVideo(pfnVideo)
            , _pfnAudio(pfnAudio)
        {
        }

        virtual void OnVideoFrame(int playedMs,
            int width, int height, void *data) override
        {
            if (_pfnVideo)
                _pfnVideo(_userdata, playedMs, width, height, data);
        }
        
        virtual void OnAudioFrame(int playedMs,
            int samplingHz, int channels, void *buf, int len) override
        {
            if (_pfnAudio)
                _pfnAudio(_userdata, playedMs, samplingHz, channels, buf, len);
        }

    private:
        void *_userdata;
        PFN_ZFP_ONVIDEO _pfnVideo;
        PFN_ZFP_ONAUDIO _pfnAudio;
    };

    PlayerPtr player(Manager::GetInst()->CreatePlayer());
    if (!player)
        return 0;

    if (!player->Open(filePath, std::make_shared<ZfpPlayerListener>(pstConfig->userdata, pstConfig->pfnVideo, pstConfig->pfnAudio),
                       !pstConfig->pfnAudio))
    {
        Manager::GetInst()->DeletePlayer(player->Id());
        return 0;
    }
    
    return player->Id();
}

FILE_PLAYER_DLL_API void Zfp_Close(int id)
{
    PlayerPtr player(Manager::GetInst()->FindPlayer(id));
    if (!player)
        return;

    player->Close();
    Manager::GetInst()->DeletePlayer(id);
}

FILE_PLAYER_DLL_API const char * Zfp_GetInfoString(int id, const char *name)
{
    PlayerPtr player(Manager::GetInst()->FindPlayer(id));
    if (!player)
        return NULL;

    return player->GetInfo(name);
}

FILE_PLAYER_DLL_API int Zfp_GetInfoInt(int id, const char *name, int dft)
{
    PlayerPtr player(Manager::GetInst()->FindPlayer(id));
    if (!player)
        return dft;

    return player->GetInfo(name, dft);
}

FILE_PLAYER_DLL_API int Zfp_RequestAudioOutput(int id,
    int samplingHz, int channcels, void *buf, int len)
{
    PlayerPtr player(Manager::GetInst()->FindPlayer(id));
    if (!player)
        return 0;

    return player->RequestAudioOutput(samplingHz, channcels, buf, len);
}

FILE_PLAYER_DLL_API bool Zfp_Start(int id)
{
    PlayerPtr player(Manager::GetInst()->FindPlayer(id));
    if (!player)
        return false;

    player->Start();
    return true;
}

FILE_PLAYER_DLL_API bool Zfp_Stop(int id)
{
    PlayerPtr player(Manager::GetInst()->FindPlayer(id));
    if (!player)
        return false;

    player->Stop();
    return true;
}

FILE_PLAYER_DLL_API bool Zfp_Pause(int id)
{
    PlayerPtr player(Manager::GetInst()->FindPlayer(id));
    if (!player)
        return false;

    player->Pause();
    return true;
}

FILE_PLAYER_DLL_API bool Zfp_Resume(int id)
{
    PlayerPtr player(Manager::GetInst()->FindPlayer(id));
    if (!player)
        return false;

    player->Resume();
    return true;
}

FILE_PLAYER_DLL_API int Zfp_Seek(int id, int mode, int ms)
{
    PlayerPtr player(Manager::GetInst()->FindPlayer(id));
    if (!player)
        return -1;

    return player->Seek(mode, ms);
}

