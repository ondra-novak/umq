/*
 * message.h
 *
 *  Created on: 23. 7. 2022
 *      Author: ondra
 */

#ifndef LIB_UMQ_MESSAGE_902ioe23ier2k323edqd
#define LIB_UMQ_MESSAGE_902ioe23ier2k323edqd
#include <string>

namespace umq {

enum class MessageType {
    text,
    binary
};

struct MessageRef {
    MessageType type;
    std::string_view data;
};

struct Message {
    MessageType type;
    std::string data;

    operator const MessageRef() const {
        return MessageRef{type, std::string_view(data)};
    }
};


}




#endif /* LIB_UMQ_MESSAGE_902ioe23ier2k323edqd */
