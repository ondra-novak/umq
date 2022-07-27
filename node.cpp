#include "node.h"

namespace umq {

void Node::init_server(PConnection &&conn, HelloRequest &&resp) {
	_hello_cb = std::move(resp);
	AbstractNode::set_connection(std::move(conn));
}

void Node::init_client(PConnection &&conn, const kjson::Value &req,
		WelcomeResponse &&resp) {
	_welcome_cb = std::move(resp);
	AbstractNode::set_connection(std::move(conn));
	AbstractNode::send_hello(req);
}

void Node::call(const std::string_view &method, const kjson::Value &params,
		ResponseCallback &&result) {

	std::unique_lock _(_callmap_lock);
	int id = _call_id++;
	std::string idstr = std::to_string(id);
	_call_map[idstr] = std::move(result);
	send_call(idstr, method, params);
}

void Node::subscribe(const std::string_view &topic, TopicUpdateCallback &&cb) {
	std::unique_lock _(_subscr_lock);

	_subscr_map.emplace(std::string(topic), std::move(cb));
}

TopicUpdateCallback Node::start_publish(const std::string_view &topic) {
	std::unique_lock _(_topic_lock);

	std::string t(topic);
	_topic_map.emplace(t, nullptr);

	return [me = weak_from_this(), t](kjson::Value data)->bool {
		auto melk = me.lock();
		if (melk != nullptr) {
			if (data.defined()) {
				std::shared_lock _(melk->_topic_lock);
				auto iter = melk->_topic_map.find(t);
				if (iter != melk->_topic_map.end()) {
					melk->send_topic_update(t,data);
					return true;
				} else{
					return false;
				}
			} else {
				std::unique_lock _(melk->_topic_lock);
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
}

bool Node::on_unsubscribe(const std::string_view &topic,
		UnsubscribeRequest &cb) {
	std::unique_lock _(_topic_lock);
	auto iter = _topic_map.find(topic);
	if (iter != _topic_map.end()) {
		iter->second = std::move(cb);
		return true;
	} else{
		return false;
	}

}

void Node::set_methods(const PMethodList &method_list) {
	std::unique_lock _(_callmap_lock);
	_methods = method_list;
}

void Node::unsubscribe(const std::string_view &topic) {
	std::unique_lock _(_subscr_lock);
	auto iter = _subscr_map.find(topic);
	if (iter != _subscr_map.end()) {
		send_unsubscribe(topic);
		_subscr_map.erase(iter);
	}
}

void Node::on_disconnect(DisconnectEvent &&disconnect) {

}

void Node::set_remote_variable(const std::string_view &name,
		const kjson::Value &value) {
}

kjson::Value Node::get_remote_variable(
		const std::string_view &name) const {
}

kjson::Value Node::get_remote_variables() const {
}

kjson::Value Node::get_local_variable(const std::string_view &name) const {
}

void Node::expect_binary(const std::string_view &hash, BinaryContentEvent &&event) {
	std::unique_lock _(_hashmap_lock);
	_hash_map.emplace(hash, std::move(event));
}

void Node::on_result(const std::string_view &id, const kjson::Value &data) {
	finish_call(id, Response::ResponseType::result, data);
}

void Node::on_welcome(const std::string_view &version, const kjson::Value &data) {
	if (_welcome_cb != nullptr) _welcome_cb(data);
}

void Node::on_exception(const std::string_view &id, const kjson::Value &data) {
	finish_call(id, Response::ResponseType::result, data);
}

void Node::on_topic_close(const std::string_view &topic_id) {
	on_topic_update(topic_id, kjson::Value());
	unsubscribe(topic_id);
}

kjson::Value Node::on_hello(const std::string_view &version, const kjson::Value &data) {
	if (_hello_cb != nullptr) return _hello_cb(data);
	else return nullptr;
}

void Node::on_unsubscribe(const std::string_view &topic_id) {
	std::unique_lock _(_topic_lock);
	auto iter = _topic_map.find(topic_id);
	if (iter != _topic_map.end()) {
		UnsubscribeRequest req = std::move(iter->second);
		_topic_map.erase(iter);
		_.unlock();
		if (req != nullptr) req(topic_id);
	}
}

bool Node::on_topic_update(const std::string_view &topic_id, const kjson::Value &data) {
	std::shared_lock _(_subscr_lock);
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

bool Node::on_call(const std::string_view &id, const std::string_view &method,
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

void Node::on_unknown_method(const std::string_view &id, const std::string_view &method_name) {
	finish_call(id, Response::ResponseType::method_not_found, method_name);
}

bool Node::on_binary_message(const umq::MessageRef &msg) {
	std::string hash = calc_hash(msg.data);
	std::unique_lock _(_hashmap_lock);
	auto iter = _hash_map.find(hash);
	if (iter != _hash_map.end()) {
		BinaryContentEvent cb = std::move(iter->second);
		_hash_map.erase(iter);
		_.unlock();
		cb(hash, msg.data);
	}
}

void Node::on_set_var(const std::string_view &variable, const kjson::Value &data) {
}

void Node::finish_call(const std::string_view &id, Response::ResponseType type,
		const kjson::Value &data) {
	std::unique_lock _(_callmap_lock);
	auto iter = _call_map.find(std::string(id));
	if (iter != _call_map.end()) {
		ResponseCallback cb = std::move(iter->second);
		_call_map.erase(iter);
		_.unlock();
		cb(Response(type, data));
	}
}

std::string Node::calc_hash(const std::string_view &bin_content) {
	throw;
}

}



