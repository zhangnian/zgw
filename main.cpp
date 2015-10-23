#include "zgw.h"

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/base/Thread.h>


int main(int argc, char* argv[])
{
    if( 2 != argc )
    {
        printf("usage:%s port\n", argv[0]);
        return -1;
    }

    LOG_INFO << "Master线程启动，TID: " << muduo::CurrentThread::tid();

    muduo::net::EventLoop loop;

    muduo::net::InetAddress listenAddr(8191);
    ZGWServer zgw_svr(&loop, listenAddr, 2);
    zgw_svr.start();

    // main线程进入事件循环
    loop.loop();

    zgw_svr.wait();

    return 0;
}
