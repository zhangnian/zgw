#include "zgw.h"

#include <errno.h>
#include <assert.h>

static const uint32_t kMaxConns = 10;


ZGWServer::ZGWServer(EventLoop* loop, InetAddress& listenAddr, CSimpleIniA& ini)
    : server_(loop, listenAddr, "zgw_server"),
      pushThr_(boost::bind(&ZGWServer::pushThreadFunc, this)),
      pullThr_(boost::bind(&ZGWServer::pullThreadFunc, this)),
      codec_(boost::bind(&ZGWServer::onClientMessage, this, _1, _2, _3)),
      ini_(ini)
{
    server_.setConnectionCallback(
        boost::bind(&ZGWServer::onClientConnection, this, _1));
    server_.setMessageCallback(
        boost::bind(&TLVCodec::onClientMessage, &codec_, _1, _2, _3));

    const char* str_num_threads = ini_.GetValue("frontend", "thread", "2");
    assert( NULL != str_num_threads );
    int num_threads = atoi(str_num_threads);

    server_.setThreadNum(num_threads);

    // 初始化ID池
    for(uint32_t i = 1; i <= kMaxConns; ++i)
    {
        idQueue_.push(i);
    }
    assert( idQueue_.size() == kMaxConns );

    zmqContext_ = zmq_ctx_new();
    assert( NULL != zmqContext_ );

    timerId_ = loop->runEvery(10.0, boost::bind(&ZGWServer::Cron, this));
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

        dispatchMsgByType(raw_msg);
    }
}


void ZGWServer::pullThreadFunc()
{
    LOG_INFO << "PULL线程启动, TID: " << muduo::CurrentThread::tid();
    assert( NULL != zmqContext_ );

    std::string pull_endpoint = ini_.GetValue("backend", "pull_service", "");
    if( pull_endpoint.size() == 0 )
    {
        LOG_ERROR << "未设置PULL socket的地址";
        return;
    }

    void* pullSocket = zmq_socket(zmqContext_, ZMQ_PULL);
    assert( NULL != pullSocket );

    int rc = zmq_bind(pullSocket, pull_endpoint.c_str());
    assert( rc == 0 );

    LOG_INFO << "绑定PULL Socket成功，地址: " << pull_endpoint;
    while(true)
    {
        zmq_msg_t msg_t;
        rc = zmq_msg_init(&msg_t);
        assert( 0 == rc );

        rc = zmq_msg_recv(&msg_t, pullSocket, 0);
        if( rc == -1 )
        {
            LOG_ERROR << "PULL线程接收消息失败, errno: " << errno;
        }
        else
        {
            responseMsg(msg_t);
        }
        zmq_msg_close(&msg_t);
    }
}


void ZGWServer::responseMsg(zmq_msg_t& msg_t)
{
    size_t msg_size = zmq_msg_size(&msg_t);
    assert( msg_size > 0 );

    std::string str_msg(static_cast<char*>(zmq_msg_data(&msg_t)), msg_size);

    ZMSG msg;
    int rc = msg.deserialize(str_msg);
    if( rc != 0 )
    {
        LOG_ERROR << "PULL线程反序列化消息失败, ret: " << rc;
        return;
    }

    LOG_INFO << "PULL线程反序列化消息成功, msg[id]: " << msg.flow_id << ", msg[type]: "
             << msg.msg_type << ", msg[body]:" << msg.msg_body;

    std::map<uint32_t, TcpConnectionPtr>::iterator iter = id2conn_.find(msg.flow_id);
    if( iter == id2conn_.end() )
    {
        LOG_ERROR << "PULL线程查找flow_id: " << msg.flow_id << "对应的连接失败";
        return;
    }
    TcpConnectionPtr conn = iter->second;

    muduo::net::Buffer buf;
    buf.prependInt8(msg.msg_type);
    buf.prependInt32(static_cast<int32_t>(msg.msg_body.size()));
    buf.append(msg.msg_body.c_str(), msg.msg_body.size());
    conn->send(&buf);

    stat_.msg_recv_cnt.increment();
    stat_.msg_recv_bytes.addAndGet(msg_size);
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
            LOG_INFO << "新连接[" << conn->peerAddress().toIpPort() << "]流水ID: " << id;
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

    CSimpleIniA::TNamesDepend keys;
    ini_.GetAllKeys("backend", keys);
    CSimpleIni::TNamesDepend::const_iterator iter = keys.begin();
    for (; iter != keys.end(); ++iter )
    {
        std::string key = iter->pItem;
        size_t pos = key.find("push_service_");
        if( pos == std::string::npos )
            continue;

        std::string str_type = key.substr(pos + 13);
        assert( str_type.size() > 0 );
        int service_type = atoi(str_type.c_str());

        std::string service_endpoint = ini_.GetValue("backend", key.c_str(), "");
        if( service_endpoint.size() == 0 )
        {
            LOG_INFO << "类型为: " << service_type << "的进程组未设置地址";
            continue;
        }

        void* pushSocket = zmq_socket(zmqContext_, ZMQ_PUSH);
        assert( NULL != pushSocket );

        int rc = zmq_bind(pushSocket, service_endpoint.c_str());
        assert( rc == 0 );

        pushSocks_[service_type] = pushSocket;
        LOG_INFO << "绑定PUSH Socket成功，类型: " << service_type << ", 地址: " << service_endpoint;
    }
}


void ZGWServer::dispatchMsgByType(ZMSG& msg)
{
    assert( msg.isVaild() );

    LOG_INFO << "PUSH线程取到消息, 开始进行分发. msg[id]: " << msg.flow_id << ", msg[type]: "
             << msg.msg_type << ", msg[body]: " << msg.msg_body;

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
    LOG_INFO << "zmq_msg_send rc: " << rc;
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
