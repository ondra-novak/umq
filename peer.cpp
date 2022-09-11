#include "peer.h"

#include <shared/trailer.h>
#include <unistd.h>
#include <charconv>
#include <sstream>
#include <thread>
namespace umq {


std::string_view Peer::version = "1.0.0";

std::size_t Peer::default_hwm = 256*1024;

Peer::Peer()
:remote(*this, nullptr)
,local(*this, &Peer::syncVar)
,context(*this, nullptr)
,_listener(*this), _hwm(default_hwm) {}

PPeer Peer::make() {
    return PPeer(new Peer());
}

void Peer::init_server(PConnection &&conn, HelloRequest &&resp) {
	_hello_cb = std::move(resp);
	_conn = std::move(conn);
	_conn->start_listen(_listener);
}

void Peer::init_client(PConnection &&conn, const Payload &req,
		WelcomeResponse &&resp) {
	_welcome_cb = std::move(resp);
	_conn = std::move(conn);
	_conn->start_listen(_listener);
	send_hello(version, req);
}

void Peer::call(const std::string_view &method, const Payload &params,
		ResponseCallback &&result) {

	std::unique_lock _(_lock);
	if (!is_connected()) {
	    result(Response(Response::Type::disconnected, Payload()));
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

TopicUpdateCallback Peer::start_publish(const std::string_view &topic, HighWaterMarkBehavior hwmb, std::size_t hwm_percent) {
	std::unique_lock _(_lock);
	std::size_t hwm_size = _hwm*hwm_percent/100;

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
        return [=, trailer = std::move(trailer), me = weak_from_this()](const Payload &data)mutable->bool {
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


void Peer::on_result(const std::string_view &id, const Payload &data) {
	finish_call(id, Response(Response::Type::result, std::move(data)));
}

void Peer::on_welcome(const std::string_view &version, const Payload &data) {
	if (_welcome_cb != nullptr) _welcome_cb(std::move(data));
}

void Peer::on_exception(const std::string_view &id, const Payload &data) {
	finish_call(id, Response(Response::Type::exception, std::move(data)));
}

void Peer::on_topic_close(const std::string_view &topic_id) {
	unsubscribe(topic_id);
}

void Peer::on_hello(const std::string_view &version, const Payload &data) {
	if (_hello_cb != nullptr) {
		_hello_cb(std::move(data),[me = weak_from_this()](const Payload &pl){
			auto melk = me.lock();
			if (melk) melk->send_welcome(Peer::version, pl);
		});
	} else {
		send_welcome(Peer::version, Payload());
	}
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

bool Peer::on_topic_update(const std::string_view &topic_id, const Payload &data) {
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

bool Peer::on_method_call(const std::string_view &id, const std::string_view &method, const Payload &args) {
    if (_methods != nullptr) {
        auto mlk = _methods.lock_shared();
        std::string strm(method);
        const MethodCall *m = mlk->find_method(strm);
        if (m) {
            (*m)(Request(weak_from_this(),id,strm,args));
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }

}

void Peer::on_execute_error(const std::string_view &id, const Payload &msg) {
	finish_call(id, Response(Response::Type::execute_error, std::move(msg)));
}

bool Peer::on_binary_message(const MsgFrame &msg) {
	if (!_dwnl_attachments.empty()) {
		Attachment a = _dwnl_attachments.front();
		_dwnl_attachments.pop();
		(*a) = std::string(msg.data);
		return true;
	} else {
		return false;
	}
}

bool Peer::on_attachment_error(const std::string_view &msg) {
	if (!_dwnl_attachments.empty()) {
		Attachment a = _dwnl_attachments.front();
		_dwnl_attachments.pop();
		(*a) = std::make_exception_ptr(std::runtime_error(std::string(msg)));
		return true;
	} else {
		return false;
	}
}

void Peer::on_unset_var(const std::string_view &variable) {
	remote.set(variable, {});
}

void Peer::on_set_var(const std::string_view &variable, const std::string_view &data) {
	remote.set(variable, std::optional<std::string>(data));
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


bool Peer::on_callback(const std::string_view &id, const std::string_view &name, const Payload &args) {
    std::unique_lock _(_lock);
    auto iter = _cb_map.find(name);
    if (iter != _cb_map.end()) {
        auto cb = std::move(iter->second);
        _cb_map.erase(iter);
        _.unlock();
        cb(Request(weak_from_this(), id, name, args));
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
        response(Response(Response::Type::disconnected, Payload()));
    } else {
        int id = _call_id++;
        std::string idstr = std::to_string(id);
        _call_map[idstr] = std::move(response);
        send_callback_call(idstr, name, args);
    }
}

void Peer::run_upload() {
	while (!_upld_attachments.empty()) {
		bool done = false;
		(*_upld_attachments.front()) >> [me = weak_from_this(), &done]
					(const AttachContent &ctx, bool async){
			auto melk = me.lock();
			if (melk) {
				try {
					const std::string &data = ctx;
					melk->send_message(MsgFrame{MsgFrameType::binary, data});
				} catch (std::exception &e) {
					melk->send_message(PeerMsgType::attachmentError, e.what());
				}
				if (async) {
					std::lock_guard _(melk->_lock);
					melk->_upld_attachments.pop();
					melk->run_upload();
				} else {
					done = true;
				}
			}
		};
		if (!done) break;
	}
}

void Peer::disconnect() {
    DisconnectEvent cb;
    Topics tpcs;
    CallMap clmp;
    std::queue<Attachment> dwn;


    {
        std::unique_lock _(_lock);
        if (is_connected()) {
            _conn.reset();
            std::swap(cb, _discnt_cb);
            std::swap(tpcs, _topic_map);
            std::swap(dwn, _dwnl_attachments);
        }
    }

    if (cb != nullptr) cb();
    for (const auto &x: tpcs) {
        if (x.second!=nullptr) x.second();
    }
    for (const auto &x: clmp) {
        if (x.second!=nullptr) x.second(Response(Response::Type::disconnected, Payload()));
    }
	while (!_dwnl_attachments.empty()) {
		Attachment a = _dwnl_attachments.front();
		_dwnl_attachments.pop();
		(*a)=std::make_exception_ptr(std::runtime_error("-1 Peer disconnected"));
	}

}

void Peer::listener_fn(const std::optional<MsgFrame> &msg) {
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


void Peer::parse_message(const MsgFrame &msg) {
    if (msg.type == MsgFrameType::binary) {
        if (!on_binary_message(msg)) {
            send_node_error(PeerError::unexpectedBinaryFrame);
        }
    } else {
    	parse_text_message(msg.data, AttachList());
    }
}
void Peer::parse_text_message(std::string_view data, AttachList &&alist) {
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
				case PeerMsgType::attachmentError:
					if (!on_attachment_error(data))
						send_node_error(PeerError::unknownMessageType);
					break;
				case PeerMsgType::attachment: {
						std::size_t cnt = 0;
						if (std::from_chars(id.data(), id.data()+id.length(), cnt, 10).ec == std::errc()) {
							for (std::size_t i = 0; i<cnt; i++) {
								auto a = std::make_shared<AttachContent>();
								alist.push_back(a);
								_dwnl_attachments.push(a);
							}
							return parse_text_message(data, std::move(alist));
						} else {
							send_node_error(PeerError::messageParseError);
						}
					}break;

				case PeerMsgType::method_call :
					name = userver::splitAt("\n", data);
					try {
					  if (!on_method_call(id, name, Payload(data,alist))) {
						  send_execute_error(id, PeerError::methodNotFound);
					  }
					} catch (const std::exception &e) {
					  send_exception(id, PeerError::unhandledException, e.what());
					}
					break;
				case PeerMsgType::callback:
					name = userver::splitAt("\n", data);
					try {
					  if (!on_callback(id, name, Payload(data,alist))) {
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
					on_result(id, Payload(data,alist));
					break;
				case PeerMsgType::exception:
					on_exception(id, Payload(data,alist));
					break;
				case PeerMsgType::execution_error:
					on_execute_error(id, Payload(data,alist));
					break;
				case PeerMsgType::topic_update:
					on_topic_update(id, Payload(data,alist));
					break;
				case PeerMsgType::unsubscribe:
					on_unsubscribe(id);
					break;
				case PeerMsgType::topic_close:
					on_topic_close(id);
					break;
				case PeerMsgType::var_set:
					on_set_var(id, Payload(data,alist));
					break;
				case PeerMsgType::var_unset:
					on_unset_var(id);
					break;
				case PeerMsgType::hello:
					if (id != version) {
						send_node_error(PeerError::unsupportedVersion);
					} else {
						on_hello(version, Payload(data,alist));
					}
					break;
				case PeerMsgType::welcome:
					if (id != version) {
						send_node_error(PeerError::unsupportedVersion);
					} else {
						on_welcome(id, Payload(data,alist));
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




bool Peer::send_topic_update(const std::string_view &topic_id,
		const Payload &data, HighWaterMarkBehavior hwmb, std::size_t hwm_size) {
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
    send_message(PeerMsgType::topic_update, topic_id, data);
    return true;
}

void Peer::send_topic_close(const std::string_view &topic_id) {
    send_message(PeerMsgType::topic_close, topic_id);
}

void Peer::send_unsubscribe(const std::string_view &topic_id) {
    send_message(PeerMsgType::unsubscribe, topic_id);
}

void Peer::send_result(const std::string_view &id, const Payload &data) {
    send_message(PeerMsgType::result, id, data);
}

void Peer::send_exception(const std::string_view &id, const Payload &data) {
    send_message(PeerMsgType::exception, id, data);
}

void Peer::send_exception(const std::string_view &id, PeerError code, const std::string_view &message) {
    send_exception(id, static_cast<int>(code), message);
}

void Peer::send_exception(const std::string_view &id, int code, const std::string_view &message) {
    send_exception(id, Payload(std::string_view(std::to_string(code).append(" ").append(message))));
}

void Peer::send_execute_error(const std::string_view &id, const Payload &msg) {
    send_message(PeerMsgType::execution_error, id, msg);
}
void Peer::send_execute_error(const std::string_view &id, PeerError code) {
    std::string s = std::to_string(static_cast<int>(code)).append(" ").append(error_to_string(code));
    send_execute_error(id,  Payload(std::string_view(s)));
}

void Peer::send_welcome(const std::string_view &version, const Payload &data) {
    send_message(PeerMsgType::welcome, version, data);
}

void Peer::send_hello(const std::string_view &version, const Payload &data) {
    send_message(PeerMsgType::hello, version, data);
}

void Peer::send_var_set(const std::string_view &variable,const std::string_view &data) {
    send_message(PeerMsgType::var_set, variable, data);
}

void Peer::send_var_unset(const std::string_view &variable) {
    send_message(PeerMsgType::var_unset, variable);
}
void Peer::send_callback_call(const std::string_view &id, const std::string_view &name, const Payload &args) {
    send_message(PeerMsgType::callback, id, name, args);
}

void Peer::send_message(const MsgFrame &msg) {
    if (!_conn) return;
    _conn->send_message(msg);
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




bool Peer::is_connected() const {
    return !!_conn;
}



void Peer::send_call(const std::string_view &id, const std::string_view &method, const Payload &params) {
    send_message(PeerMsgType::method_call, id, method, params);
}




Peer::Listener::Listener(Peer &owner):_owner(owner) {

}

void Peer::Listener::on_close() {
    _owner.disconnect();
}


void Peer::Listener::on_message(const MsgFrame &msg) {
    _owner.parse_message(msg);

}

void Peer::send_discover(const std::string_view &id, const std::string_view &method_name) {
    send_message(PeerMsgType::discover, id, method_name);
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
            send_result(id, Payload(buff.str()));
            return true;
        } else {
            std::string strm(method_name);
            const std::string *doc = mlk->find_doc(strm);
            if (doc) {
                send_result(id, std::string_view("D"+*doc));
                return true;
            } else {
                const DiscoverCall *m = mlk->find_route_discover(strm);
                if (m) {
                    (*m)(DiscoverRequest(weak_from_this(), [me=weak_from_this(), id = std::string(id)](const DiscoverResponse &resp){
                        auto lkme = me.lock();
                        if (lkme != nullptr) {
                            if (!resp.error.empty()) {
                                lkme->send_exception(id, std::string_view(resp.error));
                            } else {
                                std::ostringstream buff;
                                if (!resp.isdoc) {
                                    for (const auto &x: resp.methods) {
                                        buff << "M" << x << "\n";
                                    }
                                    for (const auto &x: resp.routes) {
                                        buff << "R" << x << "\n";
                                    }                                
                                } else {
                                    buff << "D" << resp.doc;
                                }
                                lkme->send_result(id, std::string_view(buff.str()));
                            }
                        }
                    }, id, strm));                    
                    return true;
                } else {
                    return false;
                }
            }
        }
    } else {
        send_result(id, "");
        return true;
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
            std::string_view txt = resp.get_data();
            while (!txt.empty())  {
                if (txt[0] == 'D') {
                    r.doc = txt.substr(1);
                    r.isdoc = true;
                    break;
                }
                else if (txt[0] != '\n') {
                    auto line = userver::splitAt("\n", txt);
                    switch(line[0]) {
                        case 'M': r.methods.push_back(std::string(line.substr(1))); break;
                        case 'R': r.routes.push_back(std::string(line.substr(1))); break;
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

void Peer::syncVar(const std::string_view &var, const std::optional<std::string> &value) {
	if (value.has_value()) {
		send_var_unset(var);
	} else {
		send_var_set(var, *value);
	}
}

template<typename T, typename Cmp>
inline std::optional<T> Peer::VarSpaceRO<T, Cmp>::get(const std::string_view &name) const {
	std::shared_lock _(lock());
	auto iter = _vars.find(name);
	return iter == _vars.end()?std::optional<T>():std::optional<T>(iter->second);
}

template<typename T, typename Cmp>
inline void Peer::VarSpaceRO<T, Cmp>::set(const std::string_view &name, const std::optional<T> &value) {
	bool mod = false;
	{
		std::unique_lock _(lock());
		if (!value.has_value()) {
			auto iter = _vars.find(name);
			if (iter != _vars.end()) {
				_vars.erase(iter);
				mod = true;
			}
		} else {
			auto ins = _vars.emplace(name,*value);
			if (ins.second) {
				mod = true;
			} else {
				Cmp cmp;
				if (!cmp(ins.first->second,*value)) {
					ins.first->second = *value;
					mod = true;
				}
			}
		}
	}
	if (mod && _upfn) {
		(_owner.*_upfn)(name, value);
	}
}

template<typename T, typename Cmp>
inline typename Peer::VarSpaceRO<T, Cmp>::Map Peer::VarSpaceRO<T, Cmp>::get() {
	std::shared_lock _(lock());
	return _vars;
}

template<typename T, typename Cmp>
inline void Peer::VarSpaceRO<T, Cmp>::merge(const Map &other) {
	for (const auto &x: other) {
		set(x.first, x.second);
	}
}

template<typename T, typename Cmp>
inline void Peer::VarSpaceRO<T, Cmp>::set(const Map &other) {
	if (_upfn) {
		{
			std::unique_lock _(lock());
			auto iter = _vars.begin();
			while (iter != _vars.end()) {
				if (other.find(iter->first) == other.end()) {
					(_owner.*_upfn)(iter->first, {});
					iter = _vars.erase(iter);
				} else {
					++iter;
				}
			}
		}
		merge(other);
	} else {
		std::unique_lock _(lock());
		_vars = other;
	}
}

template<typename T, typename Cmp>
inline Peer::VarSpaceRO<T, Cmp>::VarSpaceRO(Peer &owner, UpdateFn upfn)
:_owner(owner)
,_upfn(upfn)
{

}

template<typename T, typename Cmp>
inline std::shared_timed_mutex& Peer::VarSpaceRO<T, Cmp>::lock() const {
	return _owner._lock;
}

template class Peer::VarSpaceRO<std::string, std::equal_to<std::string> >;
template class Peer::VarSpaceRO<std::any, NullCmp<std::any> >;

}
