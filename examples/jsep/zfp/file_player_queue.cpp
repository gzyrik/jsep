#include "stdafx.h"
#include "file_player_queue.h"

namespace FilePlayer
{
/*
Queue::Queue()
    : _frontTs(-1)
    , _backTs(-1)
    , _queue()
{
}

Queue::~Queue()
{
}

bool Queue::Push(const ItemPtr& item)
{
    Common::RecLock lock(this);

    if (_frontTs > item->_ts)
        Clear();
    if (_queue.empty())
        _frontTs = item->_ts;
    
    _queue.push(item);
    _backTs = item->_ts + item->_len;
    
    UTIL_LOGFMT_DBG("Queue", "Push %d %lld.", _queue.size(), _backTs - _frontTs);

    return true;
}

Queue::ItemPtr Queue::Pop()
{
    Common::RecLock lock(this);
    if (_queue.empty())
        return 0;
    
    ItemPtr item = _queue.front();
    _queue.pop();

    if (_queue.empty())
    {
        _frontTs = -1;
        _backTs = -1;
    }
    else
    {
        _frontTs = _queue.front()->_ts;
    }

    UTIL_LOGFMT_DBG("Queue", "Pop %d %lld.", _queue.size(), _backTs - _frontTs);

    return item;
}

void Queue::Clear()
{
    Common::RecLock lock(this);
    while (!_queue.empty())
        _queue.pop();
    _frontTs = -1;
    _backTs = -1;
}

Common::Long Queue::FrontTs()
{
    Common::RecLock lock(this);
    return _frontTs;
}

Common::Long Queue::BackTs()
{
    Common::RecLock lock(this);
    return _backTs;
}

Common::Long Queue::TsLength()
{
    Common::RecLock lock(this);
    return _backTs - _frontTs;
}
*/
} // namespace FilePlayer

