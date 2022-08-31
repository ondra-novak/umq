#include "peer.h"

#include <shared/trailer.h>
#include <sstream>
namespace umq {


std::string_view Peer::version = "1.0.0";

std::size_t Peer::default_hwm = 16384;

Peer::Peer():_listener(*this), _hwm(default_hwm) {}

PPeer Peer::make() {
    return PPeer(new Peer());
}

void Peer::init_server(PConnection &&conn, HelloRequest &&resp) {
	_hello_cb = std::move(resp);
	_conn = std::move(conn);
	_conn->start_listen(_listener);
}

void Peer::init_client(PConnection &&conn, const std::string_view &req,
		WelcomeResponse &&resp) {
	_welcome_cb = std::move(resp);
	_conn = std::move(conn);
	_conn->start_listen(_listener);
	send_hello(version, req);
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
        return [=, trailer = std::move(trailer), me = weak_from_this()](const std::string_view &data)mutable->bool {
            auto melk = me.lock();
            if (melk != nullptr) {
                std::shared_lock _(melk->_lock);
                auto iter = melk->_topic_map.find(t);
                if (iter != melk->_topic_map.end()) {
                    return melk->send_topic_update(t, data, hwmb, hwm_size);
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

bool Peer::on_method_call(const std::string_view &id, const std::string_view &method,
		const std::string_view &args) {
    if (_methods != nullptr) {
        auto mlk = _methods.lock_shared();
        std::string strm(method);
        const MethodCall *m = mlk->find_method(strm);
        if (m) {
            (*m)(Request(weak_from_this(),id,strm,args, false));
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }

}

void Peer::on_execute_error(const std::string_view &id, const std::string_view &msg) {
	finish_call(id, Response(Response::Type::execute_error, msg));
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

void Peer::finish_call(const std::string_view &id, Response &&response) {
	std::unique_lock _(_lock);
	auto iter = _call_map.find(std::string(id));
	if (iter != _call_map.end()) {
		ResponseCallback cb = std::move(iter->second);
		_call_map.erase(iter);
		_.unlock();
		cb(std::move(response));
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

bool Peer::on_callback(const std::string_view &id, const std::string_view &name, const std::string_view &args) {
    std::unique_lock _(_lock);
    auto iter = _cb_map.find(name);
    if (iter != _cb_map.end()) {
        auto cb = std::move(iter->second);
        _cb_map.erase(iter);
        _.unlock();
        cb(Request(weak_from_this(), id, name, args, false));
        return true;
    } else {
        return false;
    }
}

std::string Peer::reg_callback(MethodCall &&c) {
    std::unique_lock _(_lock);
    int id = _call_id++;
    std::string idstr = std::to_string(id);
    idstr.append("cb");
    _cb_map[idstr] = std::move(c);
    return idstr;
}

bool Peer::unreg_callback(const std::string_view &id) {
    std::unique_lock _(_lock);
    auto iter = _cb_map.find(id);
    if (iter != _cb_map.end()) {
        _cb_map.erase(iter);
        return true;
    } else {
        return false;
    }
}

void Peer::call_callback(const std::string_view &name, const std::string_view &args, ResponseCallback &&response) {
    std::unique_lock _(_lock);
    if (!is_connected()) {
        response(Response(Response::Type::disconnected, std::string_view()));
    } else {
        int id = _call_id++;
        std::string idstr = std::to_string(id);
        _call_map[idstr] = std::move(response);
        send_callback_call(idstr, name, args);
    }
}

void Peer::disconnect() {
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

void Peer::listener_fn(const std::optional<MessageRef> &msg) {
    if (msg.has_value()) {
        parse_message(*msg);
    } else {
        disconnect();
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
    disconnect();
}


void Peer::parse_message(const MessageRef &msg) {
    if (msg.type == MessageType::binary) {
        if (!on_binary_message(msg)) {
            send_node_error(PeerError::unexpectedBinaryFrame);
        }
    } else {
        std::string_view data = msg.data;
        std::string_view topic = userver::splitAt("\n", data);
        std::string_view name;
        if (!topic.empty()) {
            char mt = topic[0];
            std::string_view id = topic.substr(1);
            try {
                switch (static_cast<PeerMsgType>(mt)) {
                    default:
                        send_node_error(PeerError::unknownMessageType);;
                        break;
                    case PeerMsgType::method_call :
                        name = userver::splitAt("\n", data);
                        try {
                          if (!on_method_call(id, name, data)) {
                              send_execute_error(id, PeerError::methodNotFound);
                          }
                        } catch (const std::exception &e) {
                          send_exception(id, PeerError::unhandledException, e.what());
                        }
                        break;
                    case PeerMsgType::callback:
                        name = userver::splitAt("\n", data);
                        try {
                          if (!on_callback(id, name, data)) {
                              send_execute_error(id, PeerError::callbackIsNotRegistered);
                          }
                        } catch (const std::exception &e) {
                          send_exception(id, PeerError::unhandledException, e.what());
                        }
                        break;
                    case PeerMsgType::discover:
                        try {
                          if (!on_discover(id, data)) {
                              send_exception(id, PeerError::methodNotFound, error_to_string(PeerError::methodNotFound));
                          }
                        } catch (const std::exception &e) {
                          send_exception(id, PeerError::unhandledException, e.what());
                        }
                        break;
                    case PeerMsgType::result:
                        on_result(id, data);
                        break;
                    case PeerMsgType::exception:
                        on_exception(id, data);
                        break;
                    case PeerMsgType::execution_error:
                        on_execute_error(id, data);
                        break;
                    case PeerMsgType::topic_update:
                        on_topic_update(id, data);
                        break;
                    case PeerMsgType::unsubscribe:
                        on_unsubscribe(id);
                        break;
                    case PeerMsgType::topic_close:
                        on_topic_close(id);
                        break;
                    case PeerMsgType::var_set:
                        on_set_var(id, data);
                        break;
                    case PeerMsgType::var_unset:
                        on_unset_var(id);
                        break;
                    case PeerMsgType::hello:
                        if (id != version) {
                            send_node_error(PeerError::unsupportedVersion);
                        } else {
                            send_welcome(version, on_hello(id, data));
                        }
                        break;
                    case PeerMsgType::welcome:
                        if (id != version) {
                            send_node_error(PeerError::unsupportedVersion);
                        } else {
                            on_welcome(id, data);
                        }
                        break;
                }
            } catch (const std::exception &e) {
                send_node_error(PeerError::messageProcessingError);
            }
        } else {
            send_node_error(PeerError::messageParseError);
        }
    }
}



bool Peer::send_topic_update(const std::string_view &topic_id, const std::string_view &data, HighWaterMarkBehavior hwmb, std::size_t hwm_size) {
    if (!_conn) return false;
    if (_conn->is_hwm(hwm_size)) {
        switch(hwmb) {
        case HighWaterMarkBehavior::block: _conn->flush();break;
        case HighWaterMarkBehavior::close: disconnect();break;
        case HighWaterMarkBehavior::ignore: break;
        case HighWaterMarkBehavior::skip: return true;
        case HighWaterMarkBehavior::unsubscribe: send_topic_close(topic_id);return false;
        }
    }
    _conn->send_message(PreparedMessage(PeerMsgType::topic_update, topic_id, {data}));
    return true;
}

void Peer::send_topic_close(const std::string_view &topic_id) {
    send_message(PreparedMessage(PeerMsgType::topic_close, topic_id, {}));
}

void Peer::send_unsubscribe(const std::string_view &topic_id) {
    send_message(PreparedMessage(PeerMsgType::unsubscribe, topic_id, {}));
}

void Peer::send_result(const std::string_view &id, const std::string_view &data) {
    send_message(PreparedMessage(PeerMsgType::result, id, {data}));
}

void Peer::send_exception(const std::string_view &id, const std::string_view &data) {
    send_message(PreparedMessage(PeerMsgType::exception, id, {data}));
}

void Peer::send_exception(const std::string_view &id, PeerError code, const std::string_view &message) {
    send_exception(id, static_cast<int>(code), message);
}

void Peer::send_exception(const std::string_view &id, int code, const std::string_view &message) {
    send_exception(id, std::to_string(code).append(" ").append(message));
}

void Peer::send_execute_error(const std::string_view &id, const std::string_view &msg) {
    send_message(PreparedMessage(PeerMsgType::execution_error, id, {msg}));
}
void Peer::send_execute_error(const std::string_view &id, PeerError code) {
    std::string s = std::to_string(static_cast<int>(code)).append(" ").append(error_to_string(code));
    send_execute_error(id,  s);
}

void Peer::send_welcome(const std::string_view &version, const std::string_view &data) {
    send_message(PreparedMessage(PeerMsgType::welcome, version, {data}));
}

void Peer::send_hello(const std::string_view &version, const std::string_view &data) {
    send_message(PreparedMessage(PeerMsgType::hello, version, {data}));
}

void Peer::send_var_set(const std::string_view &variable,const std::string_view &data) {
    send_message(PreparedMessage(PeerMsgType::var_set, variable, {data}));
}

void Peer::send_var_unset(const std::string_view &variable) {
    send_message(PreparedMessage(PeerMsgType::var_unset, variable, {}));
}
void Peer::send_callback_call(const std::string_view &id, const std::string_view &name, const std::string_view &args) {
    send_message(PreparedMessage(PeerMsgType::callback, id, {name, args}));
}

void Peer::send_message(const MessageRef &msg) {
    if (!_conn) return;
    _conn->send_message(msg);
}


Peer::PreparedMessage::PreparedMessage(PeerMsgType type, const std::string_view &topic, const std::initializer_list<std::string_view> &data)
:Message(MessageType::text)
{
    push_back(static_cast<char>(type));
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

void Peer::send_node_error(PeerError error) {
    std::unique_lock _(_lock);
    send_exception("",static_cast<int>(error),error_to_string(error));
    disconnect();
}

const char *Peer::error_to_string(PeerError err) {
    switch(err) {
    case PeerError::noError: return "No error";
    case PeerError::unexpectedBinaryFrame: return "Unexpected binary frame";
    case PeerError::messageParseError: return "Message parse error";
    case PeerError::unknownMessageType: return "Unknown message type";
    case PeerError::messageProcessingError: return "Internal node error while processing a message";
    case PeerError::unsupportedVersion: return "Unsupported version";
    case PeerError::unhandledException: return "Unhandled exception";
    case PeerError::methodNotFound: return "Method not defined";
    case PeerError::callbackIsNotRegistered: return "Callback is not registered";
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

void Peer::send_call(const std::string_view &id, const std::string_view &method, const std::string_view &params) {
    send_message(PreparedMessage(PeerMsgType::method_call, id, {method, params}));
}

void Peer::binary_flush() {
    while (!_bin_res.empty() && _bin_res.back().has_value()) {
        if (_conn) {
            if (!_conn->send_message({MessageType::binary, *_bin_res.back()})) {
                disconnect();
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

Peer::Listener::Listener(Peer &owner):_owner(owner) {

}

void Peer::Listener::on_close() {
    _owner.disconnect();
}


void Peer::Listener::on_message(const umq::MessageRef &msg) {
    _owner.parse_message(msg);

}

void Peer::send_discover(const std::string_view &id, const std::string_view &method_name) {
    send_message(PreparedMessage(PeerMsgType::discover, id, {method_name}));
}



bool Peer::on_discover(const std::string_view &id, const std::string_view &method_name) {
    if (_methods != nullptr) {
        auto mlk = _methods.lock_shared();
        if (method_name.empty()) {
            std::ostringstream buff;
            for (const auto &x: mlk->methods) {
                buff << "M" << x.first << "\n";
            }
            for (const auto &x: mlk->proxies) {
                buff << "R" << x.first << "\n";
            }
            send_result(id, buff.str());
            return true;
        } else {
            std::string strm(method_name);
            const std::string *doc = mlk->find_doc(strm);
            if (doc) {
                send_result(id, "D"+*doc);
                return true;
            } else {
                const MethodCall *m = mlk->find_method(strm);
                if (m) {
                    (*m)(Request(weak_from_this(),id,strm,"", true));
                    return true;
                } else {
                    return false;
                }
            }
        }
    } else {
        send_result(id, "");
    }

}

void Peer::discover(const std::string_view &query, DiscoverCallback  &&cb) {
    std::unique_lock _(_lock);
    if (!is_connected()) {
        DiscoverResponse r;
        r.error="Disconnected";
        cb(r);
        return;
    }
    int id = _call_id++;
    std::string idstr = std::to_string(id);
    _call_map[idstr] = [cb = std::move(cb)](Response &&resp) {
        DiscoverResponse r;
        if (resp.is_result()) {
            auto txt = resp.get_data();
            while (!txt.empty())  {
                if (txt[0] == 'D') {
                    r.doc = txt.substr(1);
                    break;
                }
                else if (txt[0] != '\n') {
                    auto line = userver::splitAt("\n", txt);
                    switch(line[0]) {
                        case 'M': r.methods.push_back(line.substr(1)); break;
                        case 'R': r.routes.push_back(line.substr(1)); break;
                        default: break;
                    }
                } else {
                    txt = txt.substr(1);
                }
            }
        } else {
            r.error = resp.get_data();
        }
        cb(r);
    };
    send_discover(idstr, query);
}

}
