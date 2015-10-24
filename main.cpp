#include "zgw.h"

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/base/Thread.h>

#include "SimpleIni.h"

int main(int argc, char* argv[])
{
    LOG_INFO << "Master线程启动，TID: " << muduo::CurrentThread::tid();
    CSimpleIniA ini;
    ini.SetUnicode();
    int ret = ini.LoadFile("./config.ini");
    if( ret < 0 )
    {
        LOG_ERROR << "加载配置文件失败，程序退出";
        return -1;
    }

    const char* str_port = ini.GetValue("frontend", "port", "1520");
    assert( str_port != NULL );
    uint16_t port = static_cast<uint16_t>(atoi(str_port));

    muduo::net::EventLoop loop;
    muduo::net::InetAddress listenAddr(port);
    ZGWServer zgw_svr(&loop, listenAddr, ini);
    zgw_svr.start();

    // main线程进入事件循环
    loop.loop();

    zgw_svr.wait();

    return 0;
}
