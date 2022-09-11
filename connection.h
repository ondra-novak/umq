/*
 * connection.h
 *
 *  Created on: 23. 7. 2022
 *      Author: ondra
 */

#ifndef LIB_UMQ_CONNECTION_H_qwepo23e2k2di902d2d
#define LIB_UMQ_CONNECTION_H_qwepo23e2k2di902d2d
#include <optional>
#include <cstddef>

#include "message.h"
namespace umq {



class AbstractConnectionListener {
public:
    virtual ~AbstractConnectionListener() = default;
    virtual void on_message(const MsgFrame &msg) = 0;
    virtual void on_close() = 0;
};




///Abstract connection for node
/** NOTE object don't need to be MT Safe. Especially send_message must be interlocked */
class AbstractConnection {
public:


    virtual ~AbstractConnection() = default;

    ///send message to the other side
    /** if failed to send message, the connection should be switched to disconnected state
     *
     * in disconnected state, messages are drop.
     *
     * @param msg message to send
     * @retval true message sent (doesn't mean, that has been delivered)
     * @retval false message was not send, connection is disconnected
     */
    virtual bool send_message(const MsgFrame &msg) = 0;

    ///Starts listening incomming messages
    /**
     * @param listener listening object.
     *
     * @note there can be only one listening object. Before the
     * connection is destroyed, you must call stop_listen to ensure, that
     * there is no pending message to be processed.
     */
    virtual void start_listen(AbstractConnectionListener &listener) = 0;

    ///tests, whether high watermark reached
    /**
     * @param v value of high watermark
     * @retval true reached
     * @retval false not reached
     */
    virtual bool is_hwm(std::size_t v)  = 0;

    ///flushes all data (synchronously)
    virtual void flush() = 0;
};

}




#endif /* LIB_UMQ_CONNECTION_H_qwepo23e2k2di902d2d */
