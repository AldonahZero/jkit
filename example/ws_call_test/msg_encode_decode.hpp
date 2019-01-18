#ifndef MSG_ENCODE_DEOCDE_HPP
#define MSG_ENCODE_DEOCDE_HPP

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include <cstdint>
#include <string>
using std::string;

#ifdef WIN32
#include <Winsock2.h>
#else
#include <endian.h>
#endif

//消息格式
//2个字节长度,表示消息类型名长度
//消息类型字符串,不以0结尾
//protobuf message序列化后的二进制编码
class ProtobufMsgCodec
{
public:
    static std::string encode(const google::protobuf::Message& message)
    {
        std::string result;
        const std::string& typeName = message.GetTypeName();
        uint16_t nameLen = static_cast<uint16_t>(typeName.size());
        uint16_t be16 = htobe16(nameLen);
        result.append(reinterpret_cast<char*>(&be16), sizeof be16);
        result.append(typeName.c_str(), nameLen);
        bool succeed = message.AppendToString(&result);

        if (succeed)
        {
            //			int32_t len = ::htonl(result.size() - kHeaderLen);
            //			std::copy(reinterpret_cast<char*>(&len),
            //				reinterpret_cast<char*>(&len) + sizeof len,
            //				result.begin());
        }
        else
        {
            result.clear();
        }

        return result;
    }

    static google::protobuf::Message* createMessage(const std::string& type_name)
    {
        google::protobuf::Message* message = nullptr;
        const google::protobuf::Descriptor* descriptor =
                google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(type_name);
        if (descriptor)
        {
            const google::protobuf::Message* prototype =
                    google::protobuf::MessageFactory::generated_factory()->GetPrototype(descriptor);
            if (prototype)
            {
                message = prototype->New();
            }
        }
        return message;
    }

    static google::protobuf::Message* decode(const std::string& buf)
    {
        return decode(buf.c_str(), buf.size());
    }

    static google::protobuf::Message* decode(const char* buf, int len)
    {
        if(len <= 4)
        {
            return nullptr;
        }

        uint16_t be;
        memcpy(&be, buf, 2);
        uint16_t nameLen = be16toh(be);
        if(nameLen < 1 || nameLen >= len - 3)
        {
            return nullptr;
        }
        std::string typeName(buf + 2, nameLen);
        google::protobuf::Message* message = createMessage(typeName);
        if(!message)
        {
            return nullptr;
        }

        if(message->ParseFromArray(buf + 2 + nameLen, len - 2 - nameLen))
        {
            return message;
        }

        delete message;
        return nullptr;
    }
};

#endif  // MSG_ENCODE_DEOCDE_HPP
