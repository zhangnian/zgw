#ifndef ZGW_H
#define ZGW_H

#include <stdio.h>
#include <unistd.h>

#include <muduo/base/Atomic.h>
#include <muduo/base/Logging.h>
#include <muduo/base/Thread.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpServer.h>
#include <muduo/base/BlockingQueue.h>

#include <boost/bind.hpp>
#include <queue>

#include "msg_def.h"
#include "tlv_codec.h"
#include "stat.h"


using namespace muduo;
using namespace muduo::net;


class ZGWServer
{
public:
    ZGWServer(EventLoop* loop, InetAddress& listenAddr, int numThreads = 2);

public:
    /**
     * 启动服务
     */
    void start();


    void wait();

private:
    /**
     * 客户端的连接到达或断开
     */
    void onClientConnection(const TcpConnectionPtr& conn);


    /**
     * 消息到达事件
     */
    void onClientMessage(const TcpConnectionPtr& conn, const ZMSG& msg, Timestamp ts);


    /**
     * PUSH线程，用于向后端进程组发送消息
     */
    void pushThreadFunc();


    /**
     * PULL线程，用于从后端进程组接收消息
     */
    void pullThreadFunc();

    /**
     * 根据消息类型，向后端进程组分发消息
     */
    void dispatchMsgByType(ZMSG& msg);

    /**
     * 创建PUSH sockets，用于向后端进程组发送消息
     */
    void createPushSocks();

    /**
     * 定时任务
     */
    void Cron();


private:
    TcpServer server_;
    muduo::Thread pushThr_;
    muduo::Thread pullThr_;
    std::queue<int> idQueue_;
    std::map<uint32_t, TcpConnectionPtr> id2conn_;
    BlockingQueue<ZMSG> outMsgQueue_;
    void* zmqContext_;
    TLVCodec codec_;  // 消息解码器
    std::map<uint8_t, void*> pushSocks_;
    Stat stat_; // 统计对象
    TimerId timerId_;
};



#endif
