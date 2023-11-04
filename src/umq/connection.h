/*
 * IConnection.h
 *
 *  Created on: 3. 11. 2023
 *      Author: ondra
 */

#ifndef SRC_UMQ_CONNECTION_H_
#define SRC_UMQ_CONNECTION_H_

#include "future.h"

namespace umq {

class IConnection {
public:
    virtual ~IConnection() = default;


    struct Message {

        enum Type {
            ///text message
            text,
            ///binary message
            binary,
            ///close message (last message in the stream) - empty data
            close

        };

        std::string_view data;
        Type type;

    };

    static constexpr Message close_message = {{},Message::close};

    ///receive a message
    /**
     * @return future with message
     */
    virtual Future<Message> receive() = 0;
    ///send a message
    /**
     * @param msg message to send
     * @retval true enqueued to be send
     * @retval false connection is already closed
     */
    virtual bool send(const Message &msg) = 0;

    ///Returns count of bytes buffered and waiting to be send
    /**
     * @return count of buffered writes
     */
    virtual std::size_t getBufferedAmount() const = 0;

    ///Waits until all buffered writes are sent
    /**
     * @retval true flushed everything
     * @retval false connection broken
     *
     */
    virtual Future<bool> flush() = 0;


};
}



#endif /* SRC_UMQ_CONNECTION_H_ */
