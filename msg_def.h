#ifndef MSG_DEF_H
#define MSG_DEF_H

#define MAX_MSG_SIZE  1024 * 64     //  64K


/**
 * 定于ZGW与后端进程交互使用的消息格式
 */
#pragma pack(1)
struct ZMSG
{
    int32_t flow_id;            // 流水号，用于匹配一次请求和响应
    uint8_t msg_type;           // 消息类型
    std::string msg_body;       // 消息内容，一般是protobuf序列化之后的字符串，也可以是json等格式

    ZMSG()
    {
        flow_id = -1;
        msg_type = 0;
        msg_body = "";
    }

    ZMSG(int32_t id, uint8_t type, const std::string& data)
        : flow_id(id), msg_type(type), msg_body(data)
    {
    }

    bool isVaild()
    {
        return flow_id != -1;
    }

    std::string serialize()
    {
        size_t msg_total_len = sizeof(flow_id) + sizeof(msg_type) + msg_body.size();

        char buf[MAX_MSG_SIZE] = { 0 };
        char* p = buf;

        memcpy(p, &flow_id, sizeof(flow_id));
        p += sizeof(flow_id);

        memcpy(p, &msg_type, sizeof(msg_type));
        p += sizeof(msg_type);

        memcpy(p, msg_body.c_str(), msg_body.size());

        std::string res;
        res.assign(buf, msg_total_len);
        return res;
    }

    int deserialize(const std::string& str)
    {
        char buf[MAX_MSG_SIZE] = { 0 };
        str.copy(buf, str.size());

        char* p = buf;
        memcpy(&flow_id, p, sizeof(flow_id));
        p += sizeof(flow_id);

        memcpy(&msg_type, p, sizeof(msg_type));
        p += sizeof(msg_type);

        msg_body.assign(p, str.size() - sizeof(flow_id) - sizeof(msg_type));
        return 0;
    }
};
#pragma pack()


#endif
