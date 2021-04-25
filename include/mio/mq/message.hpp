#pragma once

#include <vector>
#include <string>
#include <stdint.h>

#include <boost/uuid/uuid.hpp>

namespace mio
{
    namespace mq
    {
        enum class message_type : int8_t
        {
            //请求
            REQUEST = 0,
            //响应
            RESPONSE,
            //广播类型消息
            NOTICE
        };

        struct message
        {
            //消息类型
            message_type type;
            //消息uuid
            boost::uuids::uuid uuid;
            //名称
            std::string name;
            //数据
            std::vector<char> data;
        };
    }
}
