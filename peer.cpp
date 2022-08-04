#include "peer.h"

namespace umq {

std::size_t Peer::default_hwm = 16384;

Peer::Peer():_hwm(default_hwm) {}

void Peer::init_server(PConnection &&conn, HelloRequest &&resp) {
	_hello_cb = std::move(resp);
	AbstractNode::set_connection(std::move(conn));
}

void Peer::init_client(PConnection &&conn, const kjson::Value &req,
		WelcomeResponse &&resp) {
	_welcome_cb = std::move(resp);
	AbstractNode::set_connection(std::move(conn));
	AbstractNode::send_hello(req);
}

void Peer::call(const std::string_view &method, const kjson::Value &params,
		ResponseCallback &&result) {

	std::unique_lock _(_lock);
	int id = _call_id++;
	std::string idstr = std::to_string(id);
	_call_map[idstr] = std::move(result);
	_.unlock();
	send_call(idstr, method, params);
}

void Peer::subscribe(const std::string_view &topic, TopicUpdateCallback &&cb) {
	std::unique_lock _(_lock);

	_subscr_map.emplace(std::string(topic), std::move(cb));
}

TopicUpdateCallback Peer::start_publish(const std::string_view &topic) {
	std::unique_lock _(_lock);

	std::string t(topic);
	_topic_map.emplace(t, nullptr);

	return [me = weak_from_this(), t, hwm_reach = false](kjson::Value data)mutable->TopicUpdateResult  {
		auto melk = me.lock();
		if (melk != nullptr) {
			if (data.defined()) {
				std::shared_lock _(melk->_lock);
				auto iter = melk->_topic_map.find(t);
				if (iter != melk->_topic_map.end()) {
					if (hwm_reach) melk->get_connection().flush();
					melk->send_topic_update(t,data);
					hwm_reach = melk->get_connection().is_hwm(melk->_hwm);
					return hwm_reach?TopicUpdateResult::slow:TopicUpdateResult::ok;
				} else{
					return TopicUpdateResult::remove;
				}
			} else {
				std::unique_lock _(melk->_lock);
				auto iter = melk->_topic_map.find(t);
				if (iter != melk->_topic_map.end()) {
					melk->send_topic_close(t);
					return TopicUpdateResult::ok;
				} else {
					return TopicUpdateResult::remove;
				}
			}
		} else {
			return TopicUpdateResult::remove;
		}
	};
}

bool Peer::on_unsubscribe(const std::string_view &topic,
		UnsubscribeRequest &cb) {
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


void Peer::expect_binary(const std::string_view &hash, BinaryContentEvent &&event) {
	std::unique_lock _(_lock);
	_hash_map.emplace(hash, std::move(event));
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
		bool unsub = iter->second(data) == TopicUpdateResult::remove;
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
	finish_call(id, Response::ResponseType::method_not_found, method_name);
}

bool Peer::on_binary_message(const umq::MessageRef &msg) {
	std::string hash = calc_hash(msg.data);
	std::unique_lock _(_lock);
	auto iter = _hash_map.find(hash);
	if (iter != _hash_map.end()) {
		BinaryContentEvent cb = std::move(iter->second);
		_hash_map.erase(iter);
		_.unlock();
		cb(hash, msg.data);
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
	std::shared_lock _(_lock);
	if (_discnt_cb != nullptr) _discnt_cb();
}

void Peer::keep_until_disconnected() {
	on_disconnect([me=shared_from_this()]() mutable {
		me.reset();
	});
}

void Peer::set_hwm(std::size_t sz) {
	_hwm = sz;
}

std::size_t Peer::get_hwm() const {
	return _hwm;
}

Peer::~Peer() {
	AbstractNode::stop();
}

std::string Peer::calc_hash(const std::string_view &bin_content) {
	throw;
}

}



