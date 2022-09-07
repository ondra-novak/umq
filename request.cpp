#include "request.h"

#include "peer.h"
namespace umq {

RequestBase::RequestBase (const PWkPeer &peer, const std::string_view &id, const std::string_view &method_name, std::size_t extra)
:_peer(peer)
,_response_sent(false)
{
    std::size_t total =  id.size()+method_name.size()+2+extra;
    _text_data.reserve(total);
    std::copy(id.begin(), id.end(), std::back_inserter(_text_data));
    _text_data.push_back(0);
    std::copy(method_name.begin(), method_name.end(), std::back_inserter(_text_data));
    _text_data.push_back(0);
    _id = std::string_view(_text_data.data(), id.size());
    _method_name = std::string_view(_text_data.data()+id.size()+1, method_name.size());
}

RequestBase::RequestBase(RequestBase &&other)
    :_peer(std::move(other._peer))
    ,_text_data(std::move(other._text_data))
    ,_id(std::move(other._id))
    ,_method_name(std::move(other._method_name))
    ,_response_sent(std::move(other._response_sent)) {
    other._response_sent = true; 
}


Request::Request(const PWkPeer &node, const std::string_view &id,
		const std::string_view &method_name, const std::string_view &args)
:RequestBase(node, id, method_name, args.size()+1)
{
    auto pos = _text_data.size();
    std::copy(args.begin(), args.end(), std::back_inserter(_text_data));
    _text_data.push_back(0);
    this->_args = std::string_view(_text_data.data()+pos, args.size());
}

Request::~Request() {
	if (!_response_sent) {
	    send_empty_result();
	}
}

void Request::send_result(const std::string_view &val) {
	if (_response_sent) return;
	PPeer nd = _peer.lock();
	if (nd != nullptr) {
		nd->send_result(_id,val);
	}
	_response_sent = true;
}

void Request::send_exception(const std::string_view &val) {
	if (_response_sent) return;
	PPeer nd = _peer.lock();
	if (nd != nullptr) {
		nd->send_exception(_id,val);
	}
	_response_sent = true;
}

void Request::send_exception(int code, const std::string_view &message) {
    auto msg = std::to_string(code);
    msg.push_back(' ');
    msg.append(message);
	send_exception(msg);
}


void Request::send_execute_error(const std::string_view &reason) {
	if (_response_sent) return;
	PPeer nd = _peer.lock();
	if (nd != nullptr) {
		nd->send_execute_error(_id,reason);
	}
	_response_sent = true;
}


void Request::send_empty_result() {
	send_result(std::string_view());
}

std::string_view Request::get_data() const {
	return _args;
}

std::pair<int, std::string_view> Response::get_exception() const {
    char *cont;
    int code = std::strtol(_d.c_str(), &cont, 10);
    std::string_view msg(cont);
    userver::trim(msg);
    return {code, msg};
}

DiscoverRequest::DiscoverRequest(const PWkPeer &peer, Callback &&cb,
        const std::string_view &id, const std::string_view &method_name)
    :RequestBase(peer, id, method_name,0)
    ,_cb(std::move(cb))
{
}


void DiscoverRequest::send(const DiscoverResponse &resp) {
    if (!_response_sent) {
        _cb(resp);
        _response_sent = true;
    }
}

DiscoverRequest::~DiscoverRequest() {
    if (!_response_sent) {
        DiscoverResponse resp;
        send(resp);
    }
}

}
