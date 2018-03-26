#include "stdafx.h"
#include "file_player_manager.h"

static FilePlayer::Manager *g_pFilePlayerManager = NULL;

namespace FilePlayer
{

Manager::Manager()
    : _playerMap()
{
    av_register_all();
    avformat_network_init();
#ifdef _DEBUG
    //Common::setLogLevel(Common::LogDebug);
#else
    //Common::setLogLevel(Common::LogInfo);
#endif
}

Manager::~Manager()
{
}

Manager* Manager::GetInst()
{
    if (!g_pFilePlayerManager)
        g_pFilePlayerManager = new Manager();
    return g_pFilePlayerManager;
}

PlayerPtr Manager::CreatePlayer()
{
    LockGuard _(*this);
    
    int id;
    do
    {
        id = 0;// (int)Common::getRand(900000) + 100000;
        if (_playerMap.count(id) == 0)
            break;
    } while (1);
    
    PlayerPtr player = std::make_shared<Player>(id);
    _playerMap[id] = player;

    return player;
}

void Manager::DeletePlayer(int id)
{
    LockGuard _(*this);

    std::map<int, PlayerPtr>::iterator it = _playerMap.find(id);
    if (it != _playerMap.end())
        _playerMap.erase(it);
}

Player * Manager::FindPlayer(int id)
{
    LockGuard _(*this);

    std::map<int, PlayerPtr>::iterator it = _playerMap.find(id);
    if (it == _playerMap.end())
        return NULL;
    
    return it->second.get();
}

} // namespace FilePlayer

