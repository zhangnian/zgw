#ifndef STAT_H
#define STAT_H


#include <muduo/base/Atomic.h>


using muduo::AtomicInt64;

/**
 * 统计对象
 */
struct Stat
{
    AtomicInt64 msg_sent_cnt;
    AtomicInt64 msg_sent_bytes;
    AtomicInt64 msg_recv_cnt;
    AtomicInt64 msg_recv_bytes;

    void print()
    {
        LOG_INFO << "总发送消息数: " << msg_sent_cnt.get() << ", 总发送消息字节数: " << msg_sent_bytes.get()
                 << ", 总接收消息数: " << msg_recv_cnt.get() << ", 总接收消息字节数: " << msg_recv_bytes.get();
    }
};


#endif
