#include "request.h"

#include "peer.h"
namespace umq {

Request::Request(const PWkPeer &node, const std::string_view &id,
		const std::string_view &method_name, const kjson::Value &args)
:_node(node)
,_id(id)
,_method_name(method_name)
,_args(args),_response_sent(false)
{
}

Request::~Request() {
	if (!_response_sent) set_no_result();
}

void Request::set_result(const kjson::Value &val) {
	if (_response_sent) return;
	PPeer nd = _node.lock();
	if (nd != nullptr) {
		nd->send_result(_id,val);
	}
	_response_sent = true;
}

void Request::set_exception(const kjson::Value &val) {
	if (_response_sent) return;
	PPeer nd = _node.lock();
	if (nd != nullptr) {
		nd->send_exception(_id,val);
	}
	_response_sent = true;
}

void Request::set_exception(int code, const std::string_view &message) {
	set_exception(kjson::Value{
		{"code",code},
		{"message",message}
	});
}

void Request::set_exception(int code, const std::string_view &message,
		const kjson::Value &data) {
	set_exception(kjson::Value{
		{"code",code},
		{"message",message},
		{"data",data}
	});
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

kjson::Value Request::operator [](std::size_t idx) const {
	return _args[idx];
}

kjson::Value Request::operator [](std::string_view idx) const {
	return _args[idx];
}

kjson::Value Request::get_args() const {
	return _args;
}

}
