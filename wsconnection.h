/*
 * wsconnection.h
 *
 *  Created on: 1. 8. 2022
 *      Author: ondra
 */

#ifndef _LIB_UMQ_WSCONNECTION_H_eiow9834jfo3490vhns
#define _LIB_UMQ_WSCONNECTION_H_eiow9834jfo3490vhns

#include <userver/stream.h>
#include <userver/websockets_parser.h>

#include "connection.h"
#include "message.h"

namespace umq {

class WSConnection: public AbstractConnection {
public:
	WSConnection(userver::Stream &&s, bool client);
	~WSConnection();

	virtual void disconnect() override;
	virtual void start_listen(AbstractConnectionListener *listener) override;
	virtual void flush() override;
	virtual bool send_message(const MessageRef &msg) override;
	virtual bool is_hwm(std::size_t v) override;


protected:

	userver::Stream _s;
	userver::WebSocketParser _wsp;
	userver::WebSocketSerializer _wss;

	AbstractConnectionListener * _listener;

	bool _ping_send = false;
	bool _disconnected = false;
	mutable std::recursive_mutex _mx;

	void listen_cycle();

	void send_close();
	void send_ping();
	void send_pong(std::string_view data);

	void finish_write(bool ok);
};


}



#endif /* _LIB_UMQ_WSCONNECTION_H_eiow9834jfo3490vhns */
