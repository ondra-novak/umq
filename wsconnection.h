/*
 * wsconnection.h
 *
 *  Created on: 1. 8. 2022
 *      Author: ondra
 */

#ifndef _LIB_UMQ_WSCONNECTION_H_eiow9834jfo3490vhns
#define _LIB_UMQ_WSCONNECTION_H_eiow9834jfo3490vhns
#include <userver/websockets_stream.h>

#include "connection.h"

namespace umq {

class WSConnection: public AbstractConnection {
public:
    WSConnection(userver::WSStream &&stream);
    virtual void start_listen(umq::ConnectionListener &&listener) override;
    virtual void flush() override;
    virtual bool send_message(const umq::MessageRef &msg) override;
    virtual bool is_hwm(std::size_t v) override;

protected:
    userver::WSStream _s;

};



}



#endif /* _LIB_UMQ_WSCONNECTION_H_eiow9834jfo3490vhns */
