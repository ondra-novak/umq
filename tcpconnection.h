/*
 * tcpconnection.h
 *
 *  Created on: 4. 8. 2022
 *      Author: ondra
 */

#ifndef _LIB_UMQ_TCPCONNECTION_H_9032udw0du289djhioewrfj4350
#define _LIB_UMQ_TCPCONNECTION_H_9032udw0du289djhioewrfj4350
#include <userver/stream.h>
#include <atomic>
#include <cstddef>

#include "message.h"
#include "connection.h"

namespace umq {

class TCPConnection: public AbstractConnection {
public:

    TCPConnection( userver::Stream &&stream);

    TCPConnection(const TCPConnection &) = delete;
    TCPConnection &operator=(const TCPConnection &) = delete;

    ~TCPConnection();



    virtual void start_listen(umq::AbstractConnectionListener *listener) override;
    virtual void flush() override;
    virtual bool send_message(const umq::MessageRef &msg) override;
    virtual bool is_hwm(std::size_t v) override;

protected:




    enum class Type: char {
        text_frame,
        binary_frame,
        ping_frame,
        pong_frame
    };
    enum class ReadStage {
        type,
        size,
        content
    };

    AbstractConnectionListener *_listener = nullptr;
    std::atomic<bool> _disconnected = false;
    bool ping_sent = false;
    userver::Stream _stream;

    ReadStage _msg_stage = ReadStage::type;
    MessageType _msg_type = MessageType::text;
    std::size_t _msg_size = 0;
    std::string _msg_buffer;

    void disconnect();
    void finish_write(bool ok);
    void listen_cycle();
    void send_ping();
    void send_pong();



};


}




#endif /* _LIB_UMQ_TCPCONNECTION_H_9032udw0du289djhioewrfj4350 */
