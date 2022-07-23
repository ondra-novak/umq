/*
 * connection.h
 *
 *  Created on: 23. 7. 2022
 *      Author: ondra
 */

#ifndef LIB_UMQ_CONNECTION_H_qwepo23e2k2di902d2d
#define LIB_UMQ_CONNECTION_H_qwepo23e2k2di902d2d

namespace umq {

class AbstractConnectionListener {
public:

    virtual ~AbstractConnectionListener() = default;

    virtual void parse_message(const MessageRef &msg) = 0;

    virtual void on_disconnect() = 0;

};

///Abstract connection for node
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
    virtual bool send_message(const MessageRef &msg) = 0;

    ///Starts listening incomming messages
    /**
     * @param listener listening object.
     *
     * @note there can be only one listening object. Before the
     * connection is destroyed, you must call stop_listen to ensure, that
     * there is no pending message to be processed.
     */
    virtual void start_listen(AbstractConnectionListener *listener) = 0;

    ///Stops listening
    /**
     * Function blocks until there is no pending message. After return, the connection
     * can be destroyed
     */
    virtual void stop_listen() = 0;


    ///Switches connection to disconnected state
    /**
     * The disconnected state is final state, the connection must be destroyed. But
     * this allows to give to other side signal, that connection is closed and also
     * prevents to send more messages
     */
    virtual void disconnect() = 0;

};

}




#endif /* LIB_UMQ_CONNECTION_H_qwepo23e2k2di902d2d */
