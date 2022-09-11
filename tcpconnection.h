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

    virtual void start_listen(AbstractConnectionListener &listener) override;
    virtual void flush() override;
    virtual bool send_message(const MsgFrame &msg) override;
    virtual bool is_hwm(std::size_t v) override;

protected:

    userver::Stream _stream;
    
    void listener_loop(AbstractConnectionListener &listener);
    
    
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

    bool _ping_sent = false;
    bool _connected = true;
    

    ReadStage _msg_stage = ReadStage::type;
    Type _msg_type = Type::text_frame;
    std::size_t _msg_size = 0;
    std::string _msg_buffer;
    
    std::mutex _lk;
    std::string _fmt_buffer;
    
    void process_frame(AbstractConnectionListener &listener, Type type, std::string_view data);
    
    bool send_message(Type type, const std::string_view &data);
    
    void disconnect();
    void finish_write(bool ok);
    void listen_cycle();
    void send_ping();
    void send_pong(const std::string_view &data);



};


}




#endif /* _LIB_UMQ_TCPCONNECTION_H_9032udw0du289djhioewrfj4350 */
