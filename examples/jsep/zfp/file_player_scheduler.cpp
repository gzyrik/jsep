#include "stdafx.h"
#include "file_player_scheduler.h"
#include <thread>
namespace FilePlayer
{
    /*
Scheduler::Scheduler()
    : _lastTicks(0)
    , _intervalTicks(0)
    , _listener(0)
    , _pause(false)
{
}

Scheduler::~Scheduler()
{
}

bool Scheduler::Start(int64_t interval, const ListenerPtr& listener)
{
    if (thread_)
        return false;
    
    _lastTicks = 0;
    _intervalTicks = interval;
    _listener = listener;
    _pause = false;
    thread_ = new std::thread(&Scheduler::onRun, this);
    return true;
}

void Scheduler::Stop(bool wait)
{
    _listener = 0;
    thread_->join();
    thread_ = nullptr;
}

void Scheduler::Pause(bool enable)
{
    _pause = enable;
}
*/
void Scheduler::onRun()
{
    while (_listener)
    {
        int64_t now = 0;// Common::getCurTicks();
        if (now < _lastTicks + _intervalTicks)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
    
        auto listener = _listener;
        if (listener && !pause_)
            listener->OnSchd(_lastTicks ? now - _lastTicks : 0);
    
        if (_lastTicks)
            _lastTicks += _intervalTicks;
        else
            _lastTicks = now;
    }
}
} // namespace FilePlayer

