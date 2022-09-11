/*
 * message.h
 *
 *  Created on: 23. 7. 2022
 *      Author: ondra
 */

#ifndef LIB_UMQ_MESSAGE_902ioe23ier2k323edqd
#define LIB_UMQ_MESSAGE_902ioe23ier2k323edqd
#include <string>
#include <userver/helpers.h>

namespace umq {

///Definition of basic message frame type
enum class MsgFrameType {
    ///Text frame (UTF-8)
    text,
    ///Binary frame (any arbitrary binary data)
    binary,
};

///Definition of message frame
template<typename T>
struct MsgFrameT {
    ///contains message frame type
    MsgFrameType type;
    ///contains data
    T data;
};

///General message frame contains string_view to data, it doesn't carry the data
/** It is used in the most of time to avoid copying the data */
using MsgFrame = MsgFrameT<std::string_view>;
///Message frame with buffer, it also carries the message data with the object
/** If you need to transfer message with the data */
using MsgFrameBuff = MsgFrameT<std::string>;


}




#endif /* LIB_UMQ_MESSAGE_902ioe23ier2k323edqd */
