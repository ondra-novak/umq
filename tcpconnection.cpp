/*
 * tcpconnection.cpp
 *
 *  Created on: 4. 8. 2022
 *      Author: ondra
 */




#include "tcpconnection.h"

#include <future>
namespace umq {

TCPConnection::TCPConnection(userver::Stream &&stream):_stream(std::move(stream)) {
}


void TCPConnection::start_listen(AbstractConnectionListener *listener) {
    _listener = listener;
    listen_cycle();
}

void TCPConnection::flush() {
    if (_disconnected) return;
    std::promise<bool> p;
    _stream.write(std::string_view(), false) >> [&](bool x) {
        p.set_value(x);
    };
    bool z = p.get_future().get();
    if (!z) disconnect();

}

template<typename C>
void create_number(std::size_t s, C &c) {
    auto nx = s >> 7;
    auto t = s & 0x7F;
    if (nx) {
        create_number(nx, c);
        c.push_back(static_cast<char>(t) | 0x80);
    } else {
        c.push_back(static_cast<char>(t));
    }
}

bool TCPConnection::send_message(const MsgFrame &msg) {
    if (_disconnected) return false;
    std::vector<char> msgBuff;
    msgBuff.push_back(static_cast<char>(msg.type));
    create_number(msg.data.size(), msgBuff);
    msgBuff.insert(msgBuff.end(), msg.data.begin(), msg.data.end());
    std::string_view sw (msgBuff.data(), msgBuff.size());
    _stream.write(sw,false) >> [=, msgBuff = std::move(msgBuff)](bool ok) {
        finish_write(ok);
    };
    return true;

}

TCPConnection::~TCPConnection() {
    //prevents to call on_disconnect during destruction
    _disconnected = true;
}

bool TCPConnection::is_hwm(std::size_t v) {
    return _stream->get_pending_write_size() >= v;

}

void TCPConnection::disconnect() {
    bool c = false;
    if (_disconnected.compare_exchange_strong(c, true) && _listener) {
        _listener->on_disconnect();
    }
}

void TCPConnection::listen_cycle() {
    auto stream = _stream.get();
    _stream.read() >> [=](const std::string_view &data) {
        if (data.empty()) {
            if (stream->timeouted()) {
                if (ping_sent) {
                    TCPConnection::disconnect();
                    return;
                } else {
                    ping_sent = true;
                    send_ping();
                    listen_cycle();
                }
            } else {
                TCPConnection::disconnect();
                return;
            }
        } else {
            ping_sent = false;
            std::string_view d = data;
            while (!d.empty()) {
                switch(_msg_stage) {
                    default:
                    case ReadStage::type: {
                        Type t = static_cast<Type>(d[0]);
                        d = d.substr(1);
                        switch (t) {
                            default:
                            case Type::ping_frame: send_pong();
                                break;
                            case Type::pong_frame:
                                break;
                            case Type::binary_frame:
                                _msg_type = MsgFrameType::binary;
                                _msg_stage = ReadStage::size;
                                break;
                            case Type::text_frame:
                                _msg_type = MsgFrameType::text;
                                _msg_stage = ReadStage::size;
                                break;
                        }
                    }break;
                    case ReadStage::size: {
                        unsigned char c = static_cast<unsigned char>(d[0]);
                        d = d.substr(1);
                        _msg_size = (_msg_size << 7) | (c & 0x7F);
                        if (!(c & 0x80)) {
                            _msg_stage = ReadStage::content;
                        }
                    }break;
                    case ReadStage::content: {
                        std::string_view ctx = d.substr(0, _msg_size);
                        d = d.substr(ctx.size());
                        _msg_size -= ctx.size();
                        _msg_buffer.append(ctx);
                        if (_msg_size == 0) {
                            _listener->parse_message(MsgFrame{_msg_type, _msg_buffer});
                            _msg_buffer.clear();
                            _msg_stage = ReadStage::type;
                        }
                    }
                }
            }
            listen_cycle();
        }
    };
}

void TCPConnection::finish_write(bool ok) {
    if (!ok) TCPConnection::disconnect();
}

void TCPConnection::send_ping() {
   static char ping_frame = static_cast<char>(Type::ping_frame);
   _stream.write(std::string_view(&ping_frame,1), false) >> [=](bool ok){
       finish_write(ok);
   };
}

void TCPConnection::send_pong() {
    static char pong_frame = static_cast<char>(Type::pong_frame);
    _stream.write(std::string_view(&pong_frame,1), false) >> [=](bool ok){
        finish_write(ok);
    };
}

}
