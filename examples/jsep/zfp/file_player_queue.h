#pragma once
#include <cstdlib>
#include <queue>
#include <memory>
#include <mutex>
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

typedef std::shared_ptr<Queue> QueuePtr;

} // namespace FilePlayer

