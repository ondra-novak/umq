#include "peer.h"

#include <shared/trailer.h>
namespace umq {

std::size_t Peer::default_hwm = 16384;

Peer::Peer():_hwm(default_hwm) {}

void Peer::init_server(PConnection &&conn, HelloRequest &&resp) {
	_hello_cb = std::move(resp);
	_conn = std::move(conn);
	_conn->start_listen(this);
}

void Peer::init_client(PConnection &&conn, const std::string_view &req,
		WelcomeResponse &&resp) {
	_welcome_cb = std::move(resp);
	_conn = std::move(conn);
	_conn->start_listen(this);
	Peer::send_hello(version, req);
}

void Peer::call(const std::string_view &method, const std::string_view &params,
		ResponseCallback &&result) {

	std::unique_lock _(_lock);
	if (!is_connected()) {
	    result(Response(Response::Type::disconnected, std::string_view()));
        return;
	}
	int id = _call_id++;
	std::string idstr = std::to_string(id);
	_call_map[idstr] = std::move(result);
	send_call(idstr, method, params);
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

        auto trailer = ondra_shared::trailer([=,me = weak_from_this()]{
            auto melk = me.lock();
            if (melk != nullptr) {
                std::unique_lock _(melk->_lock);
                auto iter = melk->_topic_map.find(t);
                if (iter != melk->_topic_map.end()) {
                    melk->send_topic_close(t);
                }
            }
        });
        return [=, trailer = std::move(t), me = weak_from_this()](const std::string_view &data)mutable->bool {
            auto melk = me.lock();
            if (melk != nullptr) {
                std::shared_lock _(melk->_lock);
                auto iter = melk->_topic_map.find(t);
                if (iter != melk->_topic_map.end()) {
                    return melk->send_topic_update(topic, data, hwmb, hwm_size);
                } else{
                    return false;
                }
            } else {
                return false;
            }
        };
	} else {
	    return [](const std::string_view &) {
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


void Peer::on_result(const std::string_view &id, const std::string_view &data) {
	finish_call(id, Response(Response::Type::result, data));
}

void Peer::on_welcome(const std::string_view &version, const std::string_view&data) {
	if (_welcome_cb != nullptr) _welcome_cb(data);
}

void Peer::on_exception(const std::string_view &id, const std::string_view &data) {
	finish_call(id, Response(Response::Type::exception, data));
}

void Peer::on_topic_close(const std::string_view &topic_id) {
	unsubscribe(topic_id);
}

std::string Peer::on_hello(const std::string_view &version, const std::string_view &data) {
	if (_hello_cb != nullptr) return _hello_cb(data);
	else return std::string();
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

bool Peer::on_topic_update(const std::string_view &topic_id, const std::string_view &data) {
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
		const std::string_view &args) {
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

void Peer::on_execute_error(const std::string_view &id, const std::string_view &method_name) {
	finish_call(id, Response(Response::Type::execute_error, method_name));
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

void Peer::on_unset_var(const std::string_view &variable) {
    std::unique_lock _(_lock);
    auto iter = _var_map.find(variable);
    if (iter != _var_map.end()) _var_map.erase(iter);
}

void Peer::on_set_var(const std::string_view &variable, const std::string_view &data) {
	std::unique_lock _(_lock);
	_var_map[std::string(variable)] = data;
}

void Peer::finish_call(const std::string_view &id, const Response &response) {
	std::unique_lock _(_lock);
	auto iter = _call_map.find(std::string(id));
	if (iter != _call_map.end()) {
		ResponseCallback cb = std::move(iter->second);
		_call_map.erase(iter);
		_.unlock();
		cb(response);
	}
}

std::optional<std::string_view> Peer::get_peer_variable(const std::string_view &name) const {
	std::shared_lock _(_lock);
	auto iter = _var_map.find(name);
	if (iter != _var_map.end()) return std::string_view(iter->second);
	else return {};
}

void Peer::set_variable(const std::string_view &name, const std::string_view &value) {
	std::unique_lock _(_lock);
	auto iter = _local_var_map.find(name);
	if (iter == _local_var_map.end()) {
	    _local_var_map.emplace(std::string(name), std::string(value));
	    send_var_set(name, value);
	} else {
	    if (iter->second != value) {
	        iter->second.clear();
	        iter->second.append(value);
	        send_var_set(name, value);
	    }
	}
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

bool Peer::unset_variable(const std::string_view &name) {
    std::unique_lock _(_lock);
    auto iter = _local_var_map.find(name);
    if (iter == _local_var_map.end()) return false;
    _local_var_map.erase(iter);
    send_var_unset(name);
    return true;


}

void Peer::set_variables(SharedVariables &&list, bool merge) {
    std::unique_lock _(_lock);
    for (const auto &x: list) {
        auto iter = _local_var_map.find(x.first);
        if (iter == list.end() || iter->second != x.second) {
            send_var_set(x.first, x.second);
        }
    }
    if (merge) {
        for (const auto &x: _local_var_map) {
            list.emplace(x.first, x.second);
        }
    } else {
        for (const auto &x: _local_var_map) {
            auto iter = list.find(x.first);
            if (iter == list.end()) {
                send_var_unset(x.first);
            }
        }
    }
    std::swap(_local_var_map, list);
}

std::optional<std::string_view> Peer::get_variable(const std::string_view &name) const {
	std::shared_lock _(_lock);
	auto iter = _local_var_map.find(name);
	if (iter != _local_var_map.end()) {
		return std::string_view(iter->second);
	} else{
		return {};
	}
}

SharedVariables Peer::get_variables() const {
	std::shared_lock _(_lock);
	return _local_var_map;
}

SharedVariables Peer::get_peer_variables() const {
    std::shared_lock _(_lock);
    return _var_map;
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
        if (x.second!=nullptr) x.second(Response(Response::Type::disconnected, std::string_view()));
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
        std::string_view data = msg.data;
        std::string_view topic = userver::splitAt("\n", data);
        std::string_view name;
        if (!topic.empty()) {
            char mt = topic[0];
            std::string_view id = topic.substr(1);
            try {
                switch (mt) {
                    default:
                        send_node_error(NodeError::unknownMessageType);;
                        break;
                    case 'C': name = userver::splitAt("\n", data);
                              try {
                                  if (!on_call(id, name, data)) {
                                      send_unknown_method(id, name);
                                  }
                              } catch (const std::exception &e) {
                                  send_exception(id, NodeError::unhandledException, e.what());
                              }
                              break;
                    case 'P': try {
                                  on_request_continue(id, data);
                              } catch (const std::exception &e) {
                                  send_exception(id, NodeError::unhandledException, e.what());
                              }
                              break;
                    case 'I': try {
                                  on_request_info(id, data);
                              } catch (...) {
                                  //TODO: use global information channel?
                              }
                              break;
                    case 'R': on_result(id, data);
                              break;
                    case 'E': on_exception(id, data);
                              break;
                    case '?': on_execute_error(id, data);
                              break;
                    case 'T': if (!on_topic_update(id, data)) send_unsubscribe(id);
                              break;
                    case 'U': on_unsubscribe(id);
                              break;
                    case 'N': on_topic_close(id);
                              break;
                    case 'S': on_set_var(id, data);
                              break;
                    case 'X': on_unset_var(id);
                              break;
                    case 'H': if (id != version) {
                                    send_node_error(NodeError::unsupportedVersion);
                              } else {
                                  send_welcome(version, on_hello(id, data));
                              }
                              break;
                    case 'W': if (id != version) {
                                  send_node_error(NodeError::unsupportedVersion);
                              } else {
                                  on_welcome(id, data);
                              }
                              break;
                }
            } catch (const std::exception &e) {
                send_node_error(NodeError::messageProcessingError);
            }
        } else {
            send_node_error(NodeError::messageParseError);
        }
    }
}



bool Peer::send_topic_update(const std::string_view &topic_id, const std::string_view &data, HighWaterMarkBehavior hwmb, std::size_t hwm_size) {
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
    _conn->send_message(PreparedMessage('T', topic_id, {data}));
    return true;
}

void Peer::send_topic_close(const std::string_view &topic_id) {
    if (!_conn) return;
    _conn->send_message(PreparedMessage('N', topic_id, {}));
}

void Peer::send_unsubscribe(const std::string_view &topic_id) {
    if (!_conn) return;
    _conn->send_message(PreparedMessage('U', topic_id, {}));
}

void Peer::send_result(const std::string_view &id, const std::string_view &data) {
    if (!_conn) return;
    _conn->send_message(PreparedMessage('R', id, {data}));
}

void Peer::send_exception(const std::string_view &id, const std::string_view &data) {
    if (!_conn) return;
    _conn->send_message(PreparedMessage('E', id, {data}));
}

void Peer::send_exception(const std::string_view &id, NodeError code, const std::string_view &message) {
    send_exception(id, static_cast<int>(code), message);
}

void Peer::send_exception(const std::string_view &id, int code, const std::string_view &message) {
    send_exception(id, std::to_string(code).append(" ").append(message));
}

void Peer::send_unknown_method(const std::string_view &id,
        const std::string_view &method_name) {
    if (!_conn) return;
    _conn->send_message(PreparedMessage('?', id, {method_name}));
}

void Peer::send_welcome(const std::string_view &version, const std::string_view &data) {
    if (!_conn) return;
    _conn->send_message(PreparedMessage('W', version, {data}));
}

void Peer::send_hello(const std::string_view &version, const std::string_view &data) {
    if (!_conn) return;
    _conn->send_message(PreparedMessage('H', version, {data}));
}

void Peer::send_var_set(const std::string_view &variable,const std::string_view &data) {
    if (!_conn) return;
    _conn->send_message(PreparedMessage('S', variable, {data}));
}

void Peer::send_var_unset(const std::string_view &variable) {
    if (!_conn) return;
    _conn->send_message(PreparedMessage('X', variable, {}));
}

Peer::PreparedMessage::PreparedMessage(char type, const std::string_view &topic, const std::initializer_list<std::string_view> &data)
:Message(MessageType::text)
{
    push_back(type);
    append(topic);
    for (const std::string_view &x:data) {
        push_back('\n');
        append(x);
    }
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

void Peer::set_local_variable(const std::string_view &name, const std::any &value) {
    std::unique_lock _(_lock);
    auto iter = _peer_var_map.find(name);
    if (iter == _peer_var_map.end()) {
        _peer_var_map.emplace(std::string(name),value);
    } else {
        iter->second = value;
    }
}

void Peer::set_local_variable(const std::string_view &name, std::any &&value) {
    std::unique_lock _(_lock);
    auto iter = _peer_var_map.find(name);
    if (iter == _peer_var_map.end()) {
        _peer_var_map.emplace(std::string(name),std::move(value));
    } else {
        iter->second = std::move(value);
    }

}

bool Peer::unset_local_variable(const std::string_view &name) {
    std::unique_lock _(_lock);
    auto iter = _peer_var_map.find(name);
    if (iter != _peer_var_map.end()) {
        _peer_var_map.erase(iter);
        return true;
    } else {
        return false;
    }
}

void Peer::swap_local_variables(PeerVariables &&vars) {
    std::unique_lock _(_lock);
    std::swap(vars, _peer_var_map);
}

std::optional<std::any> Peer::get_local_variable(const std::string_view &name) {
    std::shared_lock _(_lock);
    auto iter = _peer_var_map.find(name);
    if (iter == _peer_var_map.end()) return {};
    else return iter->second;
}

PeerVariables Peer::get_local_variables() {
    std::shared_lock _(_lock);
    return _peer_var_map;
}

void Peer::on_request_continue(const std::string_view &id, const std::string_view &data) {
    finish_call(id,
       Response(data,
         std::make_unique<Request>(
            weak_from_this(), std::string(id),std::string(), std::string())));
}

void Peer::on_request_info(const std::string_view &id, const std::string_view &data) {
    std::unique_lock _(_lock);
    auto iter = _call_map.find(std::string(id));
    if (iter != _call_map.end()) {
        iter->second(Response(Response::Type::information,data));
    }
}

void Peer::send_info(const std::string_view &id, const std::string_view &data) {
    if (!_conn) return;
    _conn->send_message(PreparedMessage('I', id, {data}));

}

void Peer::send_continue_request(const std::string_view &id, const std::string_view &data) {
    if (!_conn) return;
    _conn->send_message(PreparedMessage('P', id, {data}));
}

void Peer::send_call(const std::string_view &id, const std::string_view &method, const std::string_view &params) {
    if (!_conn) return;
    _conn->send_message(PreparedMessage('C', id, {method, params}));
}

void Peer::continue_request(const std::string_view &id, const std::string_view &data, ResponseCallback &&cb) {
    std::unique_lock _(_lock);

    if (!is_connected()) {
        cb(Response(Response::Type::disconnected, std::string_view()));
        return;
    }
    _call_map[std::string(id)] = std::move(cb);
    send_continue_request(id, data);

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
