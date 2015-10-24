#ifndef TLV_CODEC_H
#define TLV_CODEC_H


#include <muduo/base/Logging.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/Endian.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/Endian.h>

#include <boost/function.hpp>
#include <boost/noncopyable.hpp>


#include "msg_def.h"

using namespace muduo;
using namespace muduo::net;


static const uint8_t kMsgBodyLen = sizeof(uint32_t);
static const uint8_t kMsgTypeLen = sizeof(uint8_t);
static const uint8_t kMsgHeaderLen = kMsgBodyLen + kMsgTypeLen;  // 5 bytes

static const uint32_t kMaxMsgSize = 1024 * 64;

/**
 * 消息解码器
 * 消息格式：[len][type][body]
 * len 4 bytes, type 1byes, body变长
 */
class TLVCodec : boost::noncopyable
{
public:
    typedef boost::function<void (const muduo::net::TcpConnectionPtr&,
                               const ZMSG& msg,
                               muduo::Timestamp)> StringMessageCallback;

    explicit TLVCodec(const StringMessageCallback& cb)
      : messageCallback_(cb)
    {
    }

    void onClientMessage(const muduo::net::TcpConnectionPtr& conn,
                 muduo::net::Buffer* buf,
                 muduo::Timestamp receiveTime)
    {
        // 如果缓冲区中的字节数不足一个消息头的大小，不处理
        while( buf->readableBytes() > kMsgHeaderLen )
        {
            // 注意这里字节序的转换
            uint32_t msg_len = sockets::networkToHost32(buf->peekInt32());
            uint8_t msg_type = buf->peekInt8();
            if( msg_len > kMaxMsgSize )
            {
                LOG_ERROR << "消息体长度非法, msg_len: " << msg_len;
                conn->shutdown();
                break;
            }

            // 判断缓冲区中的字节数是否满足至少一个完整消息的大小，一次while循环解析出一条消息
            if( buf->readableBytes() >= kMsgHeaderLen + msg_len )
            {
                msg_len = muduo::net::sockets::networkToHost32(buf->readInt32());
                msg_type = buf->readInt8();
                std::string msg_body(buf->peek(), msg_len);

                ZMSG msg(boost::any_cast<int>(conn->getContext()), msg_type, msg_body);
                assert( msg.isVaild() );

                messageCallback_(conn, msg, receiveTime);
                buf->retrieve(msg_len);

                LOG_INFO << "已处理一个完整的消息，缓冲区中剩余字节数为: " << buf->readableBytes();
            }
            else
            {
                LOG_INFO << "缓冲区中的字节数不足一个完整的消息";
                break;
            }
        }
    }

private:
    StringMessageCallback messageCallback_;
    const static size_t kHeaderLen = sizeof(int32_t);
};


#endif
