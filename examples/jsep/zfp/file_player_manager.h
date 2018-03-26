#pragma once

#include "file_player.h"
#include <map>
namespace FilePlayer
{

class Manager : public RecMutex
{
public:
    Manager();
    ~Manager();

    static Manager* GetInst();

    PlayerPtr CreatePlayer();
    void DeletePlayer(int id);

    Player * FindPlayer(int id);

private:
    std::map<int, PlayerPtr> _playerMap;
};

} // namespace FilePlayer

