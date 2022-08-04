#include "wsconnection.h"

#include <future>
namespace umq {

WSConnection::WSConnection(userver::Stream &&s, bool client)
:_s(std::move(s)),_wss(client)
{
}

void WSConnection::disconnect() {
	std::unique_lock _(_mx);
	if (!_disconnected) {
		_disconnected = true;
		if (_listener) _listener->on_disconnect();
	}
}

void WSConnection::start_listen(AbstractConnectionListener *listener) {
	_listener = listener;
	listen_cycle();
}

void WSConnection::flush() {
	std::unique_lock _(_mx);
	if (_disconnected) return;
	std::promise<bool> p;
	_s.write(std::string_view(), false) >> [&](bool x) {
		p.set_value(x);
	};
	_.unlock();
	bool z = p.get_future().get();
	if (!z) disconnect();
}

bool WSConnection::send_message(const MessageRef &msg) {
	std::unique_lock _(_mx);
	if (_disconnected) return false;
	switch(msg.type) {
	case MessageType::binary:
		_s.write(_wss.forgeBinaryFrame(msg.data)) >> [=](bool ok){
			finish_write(ok);
		};
		break;
	default:
	case MessageType::text:
		_s.write(_wss.forgeTextFrame(msg.data)) >> [=](bool ok){
			finish_write(ok);
		};
		break;
	}
	return !_disconnected;
}

bool WSConnection::is_hwm(std::size_t v) {
	return _s->get_pending_write_size() > v;
}

WSConnection::~WSConnection() {
	_disconnected = true;
}

void WSConnection::listen_cycle() {
	auto stream = _s.get();
	_s.read() >> [=](const std::string_view &data) {
		if (data.empty()) {
			if (stream->timeouted()) {
				if (_ping_send) {
					send_close();
					WSConnection::disconnect();
				} else {
					_ping_send = true;
					stream->clear_timeout();
					send_ping();
					listen_cycle();
				}
			} else {
				//possible exit while destruction
				WSConnection::disconnect();
			}
		} else {
			_ping_send = false;
			std::string_view rest_data = _wsp.parse(data);
			stream->put_back(rest_data);
			if (_wsp.isComplete()) {
				switch (_wsp.getFrameType()) {
				case userver::WSFrameType::binary:
					_listener->parse_message(MessageRef{
						MessageType::binary,
						_wsp.getData()
					});
					break;
				case userver::WSFrameType::connClose:
					if (!_disconnected) send_close();
					WSConnection::disconnect();
					return;
				case userver::WSFrameType::ping:
					send_pong(_wsp.getData());
					break;
				default:
				case userver::WSFrameType::pong:
					break;
				case userver::WSFrameType::text:
					_listener->parse_message(MessageRef{
						MessageType::text,
						_wsp.getData()
					});
					break;
				}
			}
			listen_cycle();
		}
	};
}

void WSConnection::send_close() {
	std::unique_lock _(_mx);
	_s.write(_wss.forgeCloseFrame(),true) >> [=](bool ok){
		finish_write(ok);
	};
}

void WSConnection::send_ping() {
	std::unique_lock _(_mx);
	_s.write(_wss.forgePingFrame(std::string_view()),true) >> [=](bool ok){
		finish_write(ok);
	};
}

void WSConnection::send_pong(std::string_view data) {
	std::unique_lock _(_mx);
	_s.write(_wss.forgePongFrame(data),true) >> [=](bool ok){
		finish_write(ok);
	};
}

void WSConnection::finish_write(bool ok) {
	if (!ok) WSConnection::disconnect();
}

}
