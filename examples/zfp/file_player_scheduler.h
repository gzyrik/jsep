#pragma once
#include <memory>
#include <thread>
#include <chrono>
#include "file_player_queue.h"
namespace FilePlayer
{

class Scheduler : public Thread
{
public:
    class Listener
    {
    public:
        virtual void OnSchd(int64_t elapse) = 0;
    };

    typedef std::shared_ptr<Listener> ListenerPtr;

    Scheduler() : _lastTicks(0), _intervalTicks(0), _listener(0), pause_(false) {}
    
    bool Start(int64_t interval, const ListenerPtr& listener) {
        _lastTicks = 0;
        _intervalTicks = interval;
        _listener = listener;
        pause_ = false;
        startRun();
        return true;
    }
    void Stop() {
        _listener = 0;
        stopRun();
    }
    void Pause(bool enable) { pause_ = enable; }
    virtual void onRun() override;
private:
    int64_t _lastTicks;
    int64_t _intervalTicks;
    ListenerPtr _listener;
    bool pause_;
};

typedef std::shared_ptr<Scheduler> SchedulerPtr;

} // namespace FilePlayer

