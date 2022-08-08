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
	if (!_response_sent) send_empty_result();
}

void Request::send_result(const std::string_view &val) {
	if (_response_sent) return;
	PPeer nd = _node.lock();
	if (nd != nullptr) {
		nd->send_result(_id,val);
	}
	_response_sent = true;
}

void Request::send_exception(const std::string_view &val) {
	if (_response_sent) return;
	PPeer nd = _node.lock();
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

void Request::send_info(const std::string_view &text) {
    if (_response_sent) return;
    PPeer nd = _node.lock();
    if (nd != nullptr) {
        nd->send_info(_id, text);
    }
}



void Request::send_execute_error(const std::string_view &reason) {
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

void Request::send_empty_result() {
	send_result(std::string_view());
}

void Request::send_request(const std::string_view &data, ResponseCallback &&cb) {

}

const std::string &Request::get_data() const {
	return _args;
}

std::pair<int, std::string_view> Response::get_exception() const {
    char *cont;
    int code = std::strtol(_d.c_str(), &cont, 10);
    std::string_view msg(cont);
    userver::trim(msg);
    return {code, msg};
}

}
