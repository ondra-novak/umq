/*
 * tcpconnection.cpp
 *
 *  Created on: 4. 8. 2022
 *      Author: ondra
 */




#include "tcpconnection.h"

#include <future>
namespace umq {

TCPConnection::TCPConnection(userver::Stream &&stream)
:_stream(userver::createBufferedStream(std::move(stream))) {}

void TCPConnection::start_listen(AbstractConnectionListener &listener) {
    listener_loop(listener);
}

void TCPConnection::listener_loop(AbstractConnectionListener &listener) {
    _stream.read() >> [this,&listener](std::string_view buff) {
        if (buff.empty()) {
            if (_stream.timeouted()) {
                if (_ping_sent) {
                    listener.on_close();
                    _connected = false;
                } else {
                    send_ping();
                    _stream.clear_timeout();
                }
            } else {
                listener.on_close();
                _connected = false;
            }
        } else {
            std::string_view data;
            _ping_sent = false;
            while (!buff.empty()) {
                switch (_msg_stage) {
                    case ReadStage::type: 
                        _msg_type = static_cast<Type>(buff[0]);
                        _msg_stage = ReadStage::size;
                        buff = buff.substr(1);
                        break;
                    case ReadStage::size: 
                        _msg_size = (_msg_size << 7) | (buff[0] & 0x7F);
                        if ((buff[0] & 0x80) == 0) {
                            _msg_stage = ReadStage::content;
                        }
                        break;
                    case ReadStage::content: 
                        data = buff.substr(0, _msg_size);
                        buff = buff.substr(data.length());
                        _msg_size-=data.size();
                        if (_msg_size) {
                            _msg_buffer.append(data);
                        } else {
                            if (_msg_buffer.empty()) {
                                process_frame(listener,_msg_type, data);
                            } else {
                                _msg_buffer.append(data);                            
                                process_frame(listener,_msg_type, _msg_buffer);
                            }
                            _msg_buffer.clear();
                        }
                }
            }
        }
    };
}

void TCPConnection::process_frame(AbstractConnectionListener &listener, Type type, std::string_view data) {
    switch(type) {
        case Type::text_frame: listener.on_message(MsgFrame{MsgFrameType::text,data});break;
        case Type::binary_frame: listener.on_message(MsgFrame{MsgFrameType::binary,data});break;
        case Type::ping_frame: send_pong(data);break;
        default:break; //ignore unknown frame
    }
}

void TCPConnection::send_ping() {
    send_message(Type::ping_frame, std::string_view());
}

void TCPConnection::flush() {
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

bool TCPConnection::send_message(Type type, const std::string_view &data) {
    std::lock_guard _(_lk);
    if (!_connected) return false;
    _fmt_buffer.push_back(static_cast<char>(type));
    create_number(data.length(), _fmt_buffer);
    _fmt_buffer.append(data);
    _connected = _stream.write_async(_fmt_buffer, nullptr);
    _fmt_buffer.clear();
    return true;
}

bool TCPConnection::send_message(const MsgFrame &msg) {
    switch(msg.type) {
        default: return false;
        case MsgFrameType::text: 
            return send_message(Type::text_frame, msg.data);            
        case MsgFrameType::binary: 
            return send_message(Type::binary_frame, msg.data);
    }
}

void TCPConnection::send_pong(const std::string_view &data) {
    send_message(Type::pong_frame, data);
}

}
