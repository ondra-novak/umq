#include "request.h"

#include "peer.h"
namespace umq {

Request::Request(const PWkPeer &node, const std::string_view &id,
		const std::string_view &method_name, const std::string_view &args)
:_node(node)
,_id(id)
,_method_name(method_name)
,_args(args),_response_sent(false)
{
}

Request::~Request() {
	if (!_response_sent) set_no_result();
}

void Request::set_result(const std::string_view &val) {
	if (_response_sent) return;
	PPeer nd = _node.lock();
	if (nd != nullptr) {
		nd->send_result(_id,val);
	}
	_response_sent = true;
}

void Request::set_exception(const std::string_view &val) {
	if (_response_sent) return;
	PPeer nd = _node.lock();
	if (nd != nullptr) {
		nd->send_exception(_id,val);
	}
	_response_sent = true;
}

void Request::set_exception(int code, const std::string_view &message) {
    auto msg = std::to_string(code);
    msg.push_back(' ');
    msg.append(message);
	set_exception(msg);
}

void Request::signal_unknown_call(const std::string_view &reason) {
	if (_response_sent) return;
	PPeer nd = _node.lock();
	if (nd != nullptr) {
		nd->send_unknown_method(_id,reason);
	}
	_response_sent = true;
}

Request::Request(Request &&req)
:_node(std::move(req._node))
,_id(std::move(req._id))
,_method_name(std::move(req._method_name))
,_args(std::move(req._args))
,_response_sent(req._response_sent)
{
	req._response_sent = true;
}

void Request::set_no_result() {
	set_result(nullptr);
}

const std::string &Request::get_data() const {
	return _args;
}

}
