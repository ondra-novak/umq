#include "publisher.h"

namespace umq {

Publisher::~Publisher() {
	reset();
}

std::size_t Publisher::subscribe(TopicUpdateCallback &cb) {
	std::unique_lock _(_mx);
	std::size_t id = ++idcnt;
	_subs.push_back({
		id, std::move(cb)
	});
	return id;
}

void Publisher::unsubscribe(std::size_t id) {
	std::unique_lock _(_mx);
	auto iter = std::lower_bound(_subs.begin(), _subs.end(), Subscriber{id},
			[](const Subscriber &a, const Subscriber &b) {
		return a.id < b.id;
	});
	if (iter != _subs.end() && iter->id == id) {
		_subs.erase(iter);
	}
}

bool Publisher::publish(const kjson::Value &v) {
	std::unique_lock _(_mx);
	auto iter = std::remove_if(_subs.begin(), _subs.end(), [&](Subscriber &s){
		auto r = s.cb(v);
		switch(r) {
		case TopicUpdateResult::ok: return false;
		case TopicUpdateResult::slow: {
			if (_enable_slow) return false;
			s.cb(kjson::Value());
			return true;
		}
		default: return true;
		}
	});
	_subs.erase(iter, _subs.end());
	return !empty();
}

void Publisher::enable_slow_subscribers(bool slow) {
	std::unique_lock _(_mx);
	_enable_slow = slow;
}

UnsubscribeRequest Publisher::create_unsub_request(std::size_t id) {
	return UnsubscribeRequest([this,id]{
		unsubscribe(id);
	});
}

UnsubscribeRequest Publisher::create_unsub_request(
		const std::shared_ptr<Publisher> &pub, std::size_t id) {
	return UnsubscribeRequest([wkme = std::weak_ptr<Publisher>(pub),id]{
		auto me = wkme.lock();
		if (me != nullptr) me->unsubscribe(id);
	});
}

void Publisher::reset() {
	std::unique_lock _(_mx);
	std::vector<Subscriber> x = std::move(_subs);
	_.unlock();
	for (auto &s: x) {
		s.cb(kjson::Value());
	}
}

bool Publisher::are_slow_subscribers_enabled() const {
	std::unique_lock _(_mx);
	return _enable_slow;
}

bool Publisher::empty() const {
	std::unique_lock _(_mx);
	return _subs.empty();
}

}
