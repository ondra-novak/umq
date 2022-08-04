#include "peer.h"

#include <kissjson/parser.h>
namespace umq {

std::size_t Peer::default_hwm = 16384;

Peer::Peer():_hwm(default_hwm) {}

void Peer::init_server(PConnection &&conn, HelloRequest &&resp) {
	_hello_cb = std::move(resp);
	_conn = std::move(conn);
	_conn->start_listen(this);
}

void Peer::init_client(PConnection &&conn, const kjson::Value &req,
		WelcomeResponse &&resp) {
	_welcome_cb = std::move(resp);
	_conn = std::move(conn);
	_conn->start_listen(this);
	Peer::send_hello(req);
}

void Peer::call(const std::string_view &method, const kjson::Value &params,
		ResponseCallback &&result) {

	std::unique_lock _(_lock);
	if (!is_connected()) {
	    result(Response(Response::ResponseType::disconnected, kjson::Value()));
        return;
	}
	int id = _call_id++;
	std::string idstr = std::to_string(id);
	_call_map[idstr] = std::move(result);
    _conn->send_message(prepareMessage('C', idstr, {method, params}));
}

void Peer::subscribe(const std::string_view &topic, TopicUpdateCallback &&cb) {
	std::unique_lock _(_lock);

	_subscr_map.emplace(std::string(topic), std::move(cb));
}

TopicUpdateCallback Peer::start_publish(const std::string_view &topic, HighWaterMarkBehavior hwmb, std::size_t hwm_size) {
	std::unique_lock _(_lock);
	if (hwm_size == 0) hwm_size = _hwm;

	if (is_connected()) {

        std::string t(topic);
        _topic_map.emplace(t, nullptr);

        return [=, me = weak_from_this()](const kjson::Value &data)mutable->bool {
            auto melk = me.lock();
            if (melk != nullptr) {
                if (data.defined()) {
                    std::shared_lock _(melk->_lock);
                    auto iter = melk->_topic_map.find(t);
                    if (iter != melk->_topic_map.end()) {
                        return melk->send_topic_update(topic, data, hwmb, hwm_size);
                    } else{
                        return false;
                    }
                } else {
                    std::unique_lock _(melk->_lock);
                    auto iter = melk->_topic_map.find(t);
                    if (iter != melk->_topic_map.end()) {
                        melk->send_topic_close(t);
                        return true;
                    } else {
                        return false;
                    }
                }
            } else {
                return false;
            }
        };
	} else {
	    return [](const kjson::Value &) {
	        return false;
	    };
	}
}

bool Peer::on_unsubscribe(const std::string_view &topic,
		UnsubscribeRequest &&cb) {
	std::unique_lock _(_lock);
	auto iter = _topic_map.find(topic);
	if (iter != _topic_map.end()) {
		iter->second = std::move(cb);
		return true;
	} else{
		return false;
	}

}

void Peer::set_methods(const PMethodList &method_list) {
	std::unique_lock _(_lock);
	_methods = method_list;
}

void Peer::unsubscribe(const std::string_view &topic) {
	std::unique_lock _(_lock);
	auto iter = _subscr_map.find(topic);
	if (iter != _subscr_map.end()) {
		send_unsubscribe(topic);
		_subscr_map.erase(iter);
	}
}

void Peer::on_disconnect(DisconnectEvent &&disconnect) {
	_discnt_cb = std::move(disconnect);
}


void Peer::on_result(const std::string_view &id, const kjson::Value &data) {
	finish_call(id, Response::ResponseType::result, data);
}

void Peer::on_welcome(const std::string_view &version, const kjson::Value &data) {
	if (_welcome_cb != nullptr) _welcome_cb(data);
}

void Peer::on_exception(const std::string_view &id, const kjson::Value &data) {
	finish_call(id, Response::ResponseType::result, data);
}

void Peer::on_topic_close(const std::string_view &topic_id) {
	on_topic_update(topic_id, kjson::Value());
	unsubscribe(topic_id);
}

kjson::Value Peer::on_hello(const std::string_view &version, const kjson::Value &data) {
	if (_hello_cb != nullptr) return _hello_cb(data);
	else return nullptr;
}

void Peer::on_unsubscribe(const std::string_view &topic_id) {
	std::unique_lock _(_lock);
	auto iter = _topic_map.find(topic_id);
	if (iter != _topic_map.end()) {
		UnsubscribeRequest req = std::move(iter->second);
		_topic_map.erase(iter);
		_.unlock();
		if (req != nullptr) req();
	}
}

bool Peer::on_topic_update(const std::string_view &topic_id, const kjson::Value &data) {
	std::shared_lock _(_lock);
	auto iter = _subscr_map.find(topic_id);
	if (iter != _subscr_map.end()) {
		bool unsub = !iter->second(data);
		if (unsub) {
			_.unlock();
			unsubscribe(topic_id);
		}
		return true;
	}
	return false;
}

bool Peer::on_call(const std::string_view &id, const std::string_view &method,
		const kjson::Value &args) {
	auto mlk = _methods.lock_shared();
	std::string strm(method);
	auto iter = mlk->find(strm);
	if (iter != mlk->end()) {
		Request req(weak_from_this(),id,strm,args);
		iter->second(req);
		return true;
	} else{
		return false;
	}

}

void Peer::on_unknown_method(const std::string_view &id, const std::string_view &method_name) {
	finish_call(id, Response::ResponseType::execute_error, method_name);
}

bool Peer::on_binary_message(const MessageRef &msg) {
	std::unique_lock _(_lock);
    std::size_t msgid = _rcv_bin_order++;
	auto iter = _bin_cbs.find(msgid);
	if (iter != _bin_cbs.end()) {
		BinaryContentEvent cb = std::move(iter->second);
		_bin_cbs.erase(iter);
		_.unlock();
		cb(true, msg.data);
		return true;
	} else {
		return false;
	}
}

void Peer::on_set_var(const std::string_view &variable, const kjson::Value &data) {
	std::unique_lock _(_lock);
	if (data.defined()) {
		_var_map[std::string(variable)] = data;
	} else {
		auto iter = _var_map.find(variable);
		if (iter != _var_map.end()) _var_map.erase(iter);
	}
}

void Peer::finish_call(const std::string_view &id, Response::ResponseType type,
		const kjson::Value &data) {
	std::unique_lock _(_lock);
	auto iter = _call_map.find(std::string(id));
	if (iter != _call_map.end()) {
		ResponseCallback cb = std::move(iter->second);
		_call_map.erase(iter);
		_.unlock();
		cb(Response(type, data));
	}
}

kjson::Value Peer::get_peer_variable(const std::string_view &name) const {
	std::shared_lock _(_lock);
	auto iter = _var_map.find(name);
	if (iter != _var_map.end()) return iter->second;
	else return kjson::Value();
}

void Peer::set_variable(const std::string_view &name, const kjson::Value &value) {
	std::unique_lock _(_lock);
	auto insr = _local_var_map.emplace(std::string(name), value);
	if (!insr.second) {
		if (insr.first->second != value) {
			send_var_set(name, value);
			insr.first->second = value;
		}
	} else {
		send_var_set(name, value);
	}
}

void Peer::set_variables(const kjson::Object &variables) {
	for (kjson::Value v : variables) {
		set_variable(v.get_key(), v.strip_key());
	}
}

kjson::Value Peer::get_variable(const std::string_view &name) const {
	std::shared_lock _(_lock);
	auto iter = _local_var_map.find(name);
	if (iter != _local_var_map.end()) {
		return iter->second;
	} else{
		return kjson::Value();
	}
}

kjson::Object Peer::get_variables() const {
	std::shared_lock _(_lock);
	return kjson::Object(_local_var_map,[&](const auto &x){
		return kjson::Value(x.first, x.second);
	});
}

kjson::Object Peer::get_peer_variables() const {
	std::shared_lock _(_lock);
	return kjson::Object(_var_map,[&](const auto &x){
		return kjson::Value(x.first, x.second);
	});
}

void Peer::on_disconnect() {
    DisconnectEvent cb;
    Topics tpcs;
    CallMap clmp;
    BinaryCallbacks bcmp;

    {
        std::unique_lock _(_lock);
        if (is_connected()) {
            _conn.reset();
            std::swap(cb, _discnt_cb);
            std::swap(tpcs, _topic_map);
            std::swap(bcmp, _bin_cbs);
        }
    }

    if (cb != nullptr) cb();
    for (const auto &x: tpcs) {
        if (x.second!=nullptr) x.second();
    }
    for (const auto &x: clmp) {
        if (x.second!=nullptr) x.second(Response(Response::ResponseType::disconnected, kjson::Value()));
    }
    for (const auto &x: bcmp) {
        if (x.second!=nullptr) x.second(false, std::string_view());
    }
}

void Peer::keep_until_disconnected() {
	on_disconnect([me=shared_from_this()]() mutable {
		me.reset();
	});
}

void Peer::set_hwm(std::size_t sz) {
    std::unique_lock _(_lock);
	_hwm = sz;
}

std::size_t Peer::get_hwm() const {
    std::shared_lock _(_lock);
	return _hwm;
}

Peer::~Peer() {
    Peer::on_disconnect();
}

std::string Peer::calc_hash(const std::string_view &bin_content) {
	throw;
}

void Peer::parse_message(const MessageRef &msg) {
    if (msg.type == MessageType::binary) {
        if (!on_binary_message(msg)) {
            send_node_error(NodeError::unexpectedBinaryFrame);
        }
    } else {
        try {
            auto v = kjson::Value::from_string(msg.data);
            if (v.is_array() && !v.empty()) {
                auto t = v[0];
                if (t.is_string()) {
                    auto strt = t.get_string();
                    kjson::Value v1, v2;
                    if (!strt.empty())  {
                        char flag = strt[0];
                        auto id = strt.substr(1);
                        try {
                            switch (flag) {
                                default:
                                    send_node_error(NodeError::unknownMessageType);
                                    break;
                                case 'C': v1 = v[1];
                                          v2 = v[2];
                                          if (v1.is_string()) {
                                              auto m = v1.get_string();
                                              try {
                                                  if (!on_call(id, m, v2)) {
                                                      send_unknown_method(id, m);
                                                  }
                                              } catch (std::exception &e) {
                                                  send_exception(id, static_cast<int>(NodeError::unhandledException),e.what());
                                              }
                                          } else {
                                              send_node_error(NodeError::invalidMesssgeFormat_Call);
                                          }
                                          break;
                                case 'R': v1 = v[1];
                                          if (v1.defined()) {
                                            on_result(id, v1);
                                          } else {
                                            send_node_error(NodeError::invalidMessageFormat_Result);
                                          }
                                          break;
                                case 'E': v1 = v[1];
                                          if (v1.defined()) {
                                            on_exception(id, v1);
                                          } else {
                                            send_node_error(NodeError::invalidMessageFormat_Exception);
                                          }
                                          break;
                                case '?': v1 = v[1];
                                          if (v1.is_string()) {
                                            on_unknown_method(id, v1.get_string());
                                          } else {
                                            send_node_error(NodeError::invalidMessageFormat_UnknownMethod);
                                          }
                                          break;
                                case 'T': v1 = v[1];
                                          if (v1.defined()) {
                                            if (!on_topic_update(id, v[1])) {
                                                    send_unsubscribe(id);
                                            }
                                          } else {
                                              send_node_error(NodeError::invalidMessageFormat_TopicUpdate);
                                          }
                                          break;
                                case 'U': on_unsubscribe(id);
                                          break;
                                case 'N': on_topic_close(id);
                                          break;
                                case 'S': on_set_var(id, v[1]);
                                          break;
                                case 'H': v1 = v[1];
                                          v2 = v[2];
                                          if (v1.get_string() != version) {
                                              send_node_error(NodeError::unsupportedVersion);
                                          } else {
                                             v1 = on_hello(v1.get_string(), v2);
                                             send_welcome(version, v1);
                                          }
                                          break;
                                case 'W': v1 = v[1];
                                          v2 = v[2];
                                          if (v1.get_string() != version) {
                                              send_node_error(NodeError::unsupportedVersion);
                                          } else {
                                              on_welcome(v1.get_string(), v2);
                                          }
                                          break;
                                }
                        } catch (const std::exception &e) {
                            send_node_error(NodeError::messageProcessingError);
                        }
                    }
                }
            }

            send_node_error(NodeError::invalidMessageFormat);

        } catch (const kjson::ParseError &err) {
            send_node_error(NodeError::messageParseError);
        }
    }
}



bool Peer::send_topic_update(const std::string_view &topic_id, const kjson::Value &data, HighWaterMarkBehavior hwmb, std::size_t hwm_size) {
    if (!_conn) return false;
    if (_conn->is_hwm(hwm_size)) {
        switch(hwmb) {
        case HighWaterMarkBehavior::block: _conn->flush();break;
        case HighWaterMarkBehavior::close: on_disconnect();break;
        case HighWaterMarkBehavior::ignore: break;
        case HighWaterMarkBehavior::skip: return true;
        case HighWaterMarkBehavior::unsubscribe: send_topic_close(topic_id);return false;
        }
    }
    _conn->send_message(prepareMessage1('T', topic_id, data));
    return true;
}

void Peer::send_topic_close(const std::string_view &topic_id) {
    if (!_conn) return;
    _conn->send_message(prepareMessage('N', topic_id));
}

void Peer::send_unsubscribe(const std::string_view &topic_id) {
    if (!_conn) return;
    _conn->send_message(prepareMessage('U', topic_id));
}

void Peer::send_result(const std::string_view &id,
        const kjson::Value &data) {
    if (!_conn) return;
    _conn->send_message(prepareMessage1('R', id, data));
}

void Peer::send_exception(const std::string_view &id,
        const kjson::Value &data) {
    if (!_conn) return;
    _conn->send_message(prepareMessage1('E', id, data));
}


void Peer::send_exception(const std::string_view &id, int code,
        const std::string_view &message) {
    send_exception("", {
            {"code", code},
            {"message", message}
    });
}

void Peer::send_unknown_method(const std::string_view &id,
        const std::string_view &method_name) {
    if (!_conn) return;
    _conn->send_message(prepareMessage1('?', id, method_name));
}

void Peer::send_welcome(const std::string_view &version,
        const kjson::Value &data) {
    if (!_conn) return;
    _conn->send_message(prepareMessage('W', "", {version,data}));
}

void Peer::send_hello(const std::string_view &version,const kjson::Value &data) {
    if (!_conn) return;
    _conn->send_message(prepareMessage('H', "", {version,data}));
}

void Peer::send_hello(const kjson::Value &data) {
    if (!_conn) return;
    _conn->send_message(prepareMessage('W', "", {version,data}));
}

void Peer::send_var_set(const std::string_view &variable,const kjson::Value &data) {
    if (!_conn) return;
    _conn->send_message(prepareMessage1('S', variable, data));
}

std::string Peer::prepareHdr(char type, const std::string_view &id) {
    return std::string(&type,1).append(id);
}

Message Peer::prepareMessage(char type, std::string_view id,kjson::Array data) {
    return Message{MessageType::text, kjson::Value::concat({kjson::Array{prepareHdr(type, id)},data}).to_string()};
}

Message Peer::prepareMessage1(char type, std::string_view id, kjson::Value data) {
    return Message{MessageType::text, kjson::Value(kjson::Array{kjson::Value(prepareHdr(type, id)),data}).to_string()};
}

void Peer::set_encoding(kjson::OutputType ot) {
    _enc = ot;
}

kjson::OutputType Peer::get_encoding() const {
    return _enc;
}

void Peer::binary_receive(std::size_t id, BinaryContentEvent &&callback) {
    std::unique_lock _(_lock);
    _bin_cbs.emplace(id, std::move(callback));
}

void Peer::send_node_error(NodeError error) {
    std::unique_lock _(_lock);
    send_exception("",static_cast<int>(error),error_to_string(error));
    on_disconnect();
}

const char *Peer::error_to_string(NodeError err) {
    switch(err) {
    case NodeError::noError: return "No error";
    case NodeError::unexpectedBinaryFrame: return "Unexpected binary frame";
    case NodeError::messageParseError: return "Message parse error";
    case NodeError::invalidMessageFormat: return "Invalid message format";
    case NodeError::invalidMesssgeFormat_Call: return "Invalid message format - message C - Call";
    case NodeError::invalidMessageFormat_Result: return "Invalid message format - message R - Result";
    case NodeError::invalidMessageFormat_Exception: return "Invalid message format - message E - Exception";
    case NodeError::invalidMessageFormat_UnknownMethod: return "Invalid message format - message ? - Unknown method";
    case NodeError::invalidMessageFormat_TopicUpdate: return "Invalid message format - message T - Topic update";
    case NodeError::unknownMessageType: return "Unknown message type";
    case NodeError::messageProcessingError: return "Internal node error while processing a message";
    case NodeError::unsupportedVersion: return "Unsupported version";
    case NodeError::unhandledException: return "Unhandled exception";
    default: return "Undetermined error";
    }
}


Message Peer::prepareMessage(char type, std::string_view id) {
    return Message{MessageType::text, kjson::Value(kjson::Array{prepareHdr(type, id)}).to_string()};
}

std::size_t Peer::binary_reserve_id() {
    std::unique_lock _(_lock);
    _bin_res.push_front(std::optional<std::string>());
    return _send_bin_order+_bin_res.size()-1;
}

void Peer::binary_send(std::size_t id, const std::string_view &data) {
    std::unique_lock _(_lock);
    auto iter = _bin_res.begin() + (id - _send_bin_order);
    iter->emplace(data);
    binary_flush();
}

bool Peer::is_connected() const {
    return !!_conn;
}

void Peer::binary_send(std::size_t id, std::string &&data) {
    std::unique_lock _(_lock);
    auto iter = _bin_res.begin() + (id - _send_bin_order);
    iter->emplace(std::move(data));
    binary_flush();
}

void Peer::binary_flush() {
    while (!_bin_res.empty() && _bin_res.back().has_value()) {
        if (_conn) {
            if (!_conn->send_message({MessageType::binary, *_bin_res.back()})) {
                on_disconnect();
            }
        }
        _send_bin_order++;
        _bin_res.pop_back();
    }
}


Peer::BinaryMessage::BinaryMessage(BinaryMessage &&other)
:_peer(std::move(other._peer)),_id(std::move(other._id))
{
}

Peer::BinaryMessage &Peer::BinaryMessage::operator =(BinaryMessage &&other) {
    if (this != &other) {
        auto p = _peer.lock();
        if (p) {
            p->binary_send(_id, std::string());
        }
        _peer = std::move(other._peer);
        _id = std::move(other._id);
    }
    return *this;
}

Peer::BinaryMessage::~BinaryMessage() {
    auto p = _peer.lock();
    if (p) {
        p->binary_send(_id, std::string());
    }
}

std::size_t Peer::BinaryMessage::get_id() const {
    return _id;
}

void Peer::BinaryMessage::send(const std::string_view &data) {
    auto p = _peer.lock();
    if (p) {
        p->binary_send(_id, data);
        _peer = PWkPeer();
    }
}

void Peer::BinaryMessage::send(std::string &&data) {
    auto p = _peer.lock();
    if (p) {
        p->binary_send(_id, std::move(data));
        _peer = PWkPeer();
    }
}


Peer::BinaryMessage::BinaryMessage(const PPeer &peer)
    :_peer(peer), _id(peer->binary_reserve_id())
{

}

Peer::BinaryMessage::BinaryMessage(const PWkPeer &peer)
    :_peer(peer),_id(0)
{
    auto p = _peer.lock();
    if (p) _id = p->binary_reserve_id();
}


}