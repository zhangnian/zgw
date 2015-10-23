#include "zgw.h"

#include <errno.h>
#include <assert.h>
#include <zmq.h>

static const uint32_t kMaxConns = 10;




ZGWServer::ZGWServer(EventLoop* loop, InetAddress& listenAddr, int numThreads)
    : server_(loop, listenAddr, "zgw_server"),
      pushThr_(boost::bind(&ZGWServer::pushThreadFunc, this)),
      pullThr_(boost::bind(&ZGWServer::pullThreadFunc, this)),
      codec_(boost::bind(&ZGWServer::onClientMessage, this, _1, _2, _3))
{
    server_.setConnectionCallback(
        boost::bind(&ZGWServer::onClientConnection, this, _1));
    server_.setMessageCallback(
        boost::bind(&TLVCodec::onClientMessage, &codec_, _1, _2, _3));
    server_.setThreadNum(numThreads);

    // 初始化ID池
    for(uint32_t i = 1; i <= kMaxConns; ++i)
    {
        idQueue_.push(i);
    }
    assert( idQueue_.size() == kMaxConns );

    zmqContext_ = zmq_ctx_new();
    assert( NULL != zmqContext_ );

    timerId_ = loop->runEvery(3.0, boost::bind(&ZGWServer::Cron, this));
}


void ZGWServer::start()
{
    pushThr_.start();
    pullThr_.start();

    // 启动TcpServer
    server_.start();
}


void ZGWServer::wait()
{
    pushThr_.join();
    pullThr_.join();
}


void ZGWServer::pushThreadFunc()
{
    LOG_INFO << "PUSH线程启动, TID: " << muduo::CurrentThread::tid();

    createPushSocks();

    while(true)
    {
        // 消息队列中无消息是，阻塞等待
        ZMSG raw_msg = outMsgQueue_.take();
        assert( raw_msg.isVaild() );

        LOG_INFO << "PUSH线程取到消息, 准备发送. msg[id]: " << raw_msg.flow_id << ", msg[type]: "
                 << raw_msg.msg_type << ", msg[body]: " << raw_msg.msg_body;

        dispatchMsgByType(raw_msg);
    }
}


void ZGWServer::pullThreadFunc()
{
    LOG_INFO << "PULL线程启动, TID: " << muduo::CurrentThread::tid();
    assert( NULL != zmqContext_ );

    void* pullSocket = zmq_socket(zmqContext_, ZMQ_PULL);
    assert( NULL != pullSocket );

    int rc = zmq_bind(pullSocket, "tcp://127.0.0.1:5589");
    assert( rc == 0 );

    LOG_INFO << "PULL线程绑定成功";
}


void ZGWServer::onClientConnection(const TcpConnectionPtr& conn)
{
    LOG_INFO << "Client " << conn->peerAddress().toIpPort() << " -> "
        << conn->localAddress().toIpPort() << " is "
        << (conn->connected() ? "UP" : "DOWN");

    if( conn->connected() )
    {
        int id = -1;
        if( !idQueue_.empty() )
        {
            id = idQueue_.front();
            idQueue_.pop();
            id2conn_[id] = conn;
        }

        if( -1 == id )
        {
            LOG_ERROR << "[过载保护]系统到达最大连接数，拒绝连接";
            conn->shutdown();
        }
        else
        {
            conn->setContext(id);
            LOG_INFO << "conn id: " << id;
        }
    }
    else
    {
        if(!conn->getContext().empty())
        {
            int id = boost::any_cast<int>(conn->getContext());
            assert(id > 0 && id <= static_cast<int>(kMaxConns));

            // 回收连接的key
            idQueue_.push(id);
            id2conn_.erase(id);
        }
    }
}

void ZGWServer::onClientMessage(const TcpConnectionPtr& conn, const ZMSG& msg, Timestamp ts)
{
    // 注意，此函数在I/O线程中调用，不能做CPU密集型任务，以免阻塞事件循环
    outMsgQueue_.put(msg);
    LOG_INFO << "消息入队成功, msg[id]: " << msg.flow_id << ", msg[type]: " << msg.msg_type << ", msg[size]: " << msg.msg_body.size();
}


void ZGWServer::createPushSocks()
{
    assert( NULL != zmqContext_ );

    void* pushSocket = zmq_socket(zmqContext_, ZMQ_PUSH);
    assert( NULL != pushSocket );

    int rc = zmq_bind(pushSocket, "tcp://127.0.0.1:11111");
    assert( rc == 0 );

    pushSocks_[1] = pushSocket;
}


void ZGWServer::dispatchMsgByType(ZMSG& msg)
{
    assert( msg.isVaild() );

    std::map<uint8_t, void*>::iterator iter = pushSocks_.find(msg.msg_type);
    if( iter == pushSocks_.end() )
    {
        LOG_ERROR << "未找到消息类型: " << msg.msg_type << "对应的发送通道";
        return;
    }

    void* pushSocket = iter->second;
    assert( NULL != pushSocket );

    std::string str_msg = msg.serialize();
    assert( str_msg.size() > 0 );

    zmq_msg_t msg_t;
    int rc = zmq_msg_init(&msg_t);
    assert( 0 == rc );

    zmq_msg_init_data(&msg_t, const_cast<char*>(str_msg.c_str()), str_msg.size(), NULL, NULL);
    rc = zmq_msg_send(&msg_t, pushSocket, ZMQ_NOBLOCK);
    if( rc == -1 )
    {
        int err = zmq_errno();
        if( err == EAGAIN )
        {
            LOG_ERROR << "对端未连接，消息被缓存到本地内存中";
        }
        else
        {
            LOG_ERROR << "PUSH线程发送消息失败, errno: " << err;
        }
    }
    zmq_msg_close(&msg_t);

    // 更新统计信息
    stat_.msg_sent_cnt.increment();
    stat_.msg_sent_bytes.addAndGet(str_msg.size());

}


void ZGWServer::Cron()
{
    stat_.print();
}
