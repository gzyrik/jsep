#pragma once
#define UTIL_LOGFMT_ERR(...)
#define UTIL_LOGFMT_IFO(...)
#define UTIL_LOGFMT_DBG(...)
extern "C"
{
#include <third_party/ffmpeg/libavcodec/avcodec.h>
#include <third_party/ffmpeg/libavformat/avformat.h>
#include <third_party/ffmpeg/libavutil/imgutils.h>
#include <third_party/ffmpeg/libswscale/swscale.h>
#include <third_party/ffmpeg/libswresample/swresample.h>
#include <third_party/ffmpeg/libavutil/opt.h>
};
#include <queue>
#include <memory>
#include <mutex>
namespace rtc { int64_t TimeMillis(); }
namespace FilePlayer
{
class RecMutex
{
private:
    mutable std::recursive_mutex mutex_;
public:
    virtual ~RecMutex() {}
    void lock() const { mutex_.lock(); }
    void unlock() const { mutex_.unlock(); }
};
typedef std::lock_guard<const RecMutex> LockGuard;
class Thread {
private:
    std::thread* thread_;
public:
    void startRun() { thread_ = new std::thread(&Thread::onRun, this);}
    virtual void onRun() = 0;
    void stopRun() {
        if (thread_) {
            auto t = thread_;
            thread_ = nullptr;
            t->join();
        }
    }
    bool isRunning() const { return thread_ != nullptr; }
    virtual ~Thread(){ stopRun(); }
};
class Queue : public RecMutex
{
public:
    struct Item
    {
        Item(int64_t ts, int64_t len) : _ts(ts), _len(len) {};
        virtual ~Item() {};
        int64_t _ts;
        int64_t _len;
    };
    typedef std::shared_ptr<Item> ItemPtr;
    Queue() :frontTs_(-1), backTs_(-1) {}

    void Push(const ItemPtr& item) {
        LockGuard _(*this);
        if (frontTs_ > item->_ts)
            Clear();
        if (queue_.empty())
            frontTs_ = item->_ts;

        queue_.push(item);
        backTs_ = item->_ts + item->_len;
    }
    ItemPtr Pop() {
        LockGuard _(*this);
        if (queue_.empty())
            return nullptr;

        ItemPtr item = queue_.front();
        queue_.pop();

        if (queue_.empty())
            frontTs_ = backTs_  = -1;
        else
            frontTs_ = queue_.front()->_ts;
        return item;
    }
    void Clear() {
        LockGuard _(*this);
        while (!queue_.empty()) queue_.pop();
        frontTs_ = backTs_  = -1;
    }
    int64_t FrontTs() const { return frontTs_; }
    int64_t BackTs() const { return backTs_;  }
    int64_t TsLength() const  { LockGuard _(*this); return backTs_ - frontTs_; }
private:
    int64_t frontTs_;
    int64_t backTs_;
    std::queue<ItemPtr> queue_;
};
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
    
    void Start(int64_t interval, const ListenerPtr& listener) {
        _lastTicks = 0;
        _intervalTicks = interval;
        _listener = listener;
        pause_ = false;
        startRun();
    }
    void Stop() {
        stopRun();
        _listener = 0;
    }
    void Pause(bool enable) { pause_ = enable; }
    virtual void onRun() override {
        while (isRunning()) {
            int64_t now = rtc::TimeMillis();
            if (now < _lastTicks + _intervalTicks)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            if (!pause_)
                _listener->OnSchd(_lastTicks ? now - _lastTicks : 0);

            if (_lastTicks)
                _lastTicks += _intervalTicks;
            else
                _lastTicks = now;
        }
    }

private:
    int64_t _lastTicks;
    int64_t _intervalTicks;
    ListenerPtr _listener;
    bool pause_;
};

typedef std::shared_ptr<Scheduler> SchedulerPtr;
typedef std::shared_ptr<Queue> QueuePtr;

} // namespace FilePlayer

