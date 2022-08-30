#include "publisher.h"

namespace umq {

Publisher::~Publisher() {
	reset();
}

std::size_t Publisher::subscribe(TopicUpdateCallback &&cb) {
	std::unique_lock _(_mx);
	std::size_t id = ++idcnt;
	_subs.push_back({
		id, std::move(cb)
	});
	return id;
}

void Publisher::unsubscribe(std::size_t id) {
	std::unique_lock _(_mx);
	auto iter = std::lower_bound(_unsubs.begin(), _unsubs.end(), id,
	        [](std::size_t a, std::size_t b) {return a<b;});
	_unsubs.insert(iter, id);
	if (!_inp) do_unsubscribe();
}

bool Publisher::publish(const std::string_view &v) {
	std::unique_lock _(_mx);
	_inp = true;
	for (const auto &x: _subs) {
	    try {
	        if (!x.cb(v)) _unsubs.push_back(x.id);
	    } catch (...) {
	        _unsubs.push_back(x.id);
	    }
	}
	do_unsubscribe();
	_inp = false;
	return !_subs.empty();
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
        s.cb(std::string_view());
    }
    _unsubs.clear();
}

bool Publisher::empty() const {
	std::unique_lock _(_mx);
	return _subs.empty();
}

void Publisher::do_unsubscribe() {
    auto iter =  _unsubs.begin();
    auto iend = _unsubs.end();
    //_unsubs is ordered by id
    //_subs is ordered by id
    auto r = std::remove_if(_subs.begin(),_subs.end(), [&](const Subscriber &s){
        while (iter != iend && *iter < s.id) {
            ++iter;
        }
        if (iter != iend && *iter == s.id) {
                ++iter;return true;
        } else {
            return false;
        }
    });
    _unsubs.clear();
    _subs.erase(r, _subs.end());
}

}
