#include "request.h"

#include "peer.h"
namespace umq {


Request::Request(const PWkPeer &node, const std::string_view &id,
		const std::string_view &method_name, const std::string_view &args,
		bool discover_request)
:_node(node)
,_response_sent(false)
,_is_discover_request(discover_request)
{
    _string_data.reserve(id.size()+method_name.size()+args.size()+3);
    _string_data.insert(_string_data.end(), id.begin(), id.end());
    _string_data.push_back(0);
    _string_data.insert( _string_data.end(), method_name.begin(), method_name.end());
    _string_data.push_back(0);
    _string_data.insert(_string_data.end(), args.begin(), args.end());
    _string_data.push_back(0);
    std::string_view whole(_string_data.data(), _string_data.size());
    _id = whole.substr(0,id.size());
    _method_name = whole.substr(id.size()+1, method_name.size());
    _args = whole.substr(id.size()+1+method_name.size()+1, args.size());
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


void Request::send_execute_error(const std::string_view &reason) {
	if (_response_sent) return;
	PPeer nd = _node.lock();
	if (nd != nullptr) {
		nd->send_execute_error(_id,reason);
	}
	_response_sent = true;
}

Request::Request(Request &&req)
:_node(std::move(req._node))
,_response_sent(std::move(req._response_sent))
,_string_data(std::move(req._string_data))
,_id(std::move(req._id))
,_method_name(std::move(req._method_name))
,_args(std::move(req._args))
{
	req._response_sent = true;
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

}
