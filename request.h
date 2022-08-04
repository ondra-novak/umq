#ifndef LIB_UMQ_REQUEST_H_qwpodj023jd9d928dw
#define LIB_UMQ_REQUEST_H_qwpodj023jd9d928dw
#include <kissjson/value.h>
#include <memory>
#include <string_view>
#include <shared/callback.h>

namespace umq {

class Peer;

using PPeer = std::shared_ptr<Peer>;
using PWkPeer = std::weak_ptr<Peer>;

enum class TopicUpdateResult {
	///operation ok - data published
	ok,
	///operation ok - however HWM reached, next publish will be slowed down
	slow,
	///remove subscriber from the publisher
	remove,
};

///Callback when topic update - but if data are 'undefined', then topic is closed
/** @param data - data of topic
 * @retval true continue receive topic
 * @retval false stop receive topic (unsubscribe)
 */
using TopicUpdateCallback = ondra_shared::Callback<TopicUpdateResult(kjson::Value data)>;


class Request {
public:
	Request(const PWkPeer &node,
			const std::string_view &id,
			const std::string_view &method_name,
			const kjson::Value &args);

	~Request();

	Request (const Request &req) = delete;
	Request (Request &&req);
	Request &operator=(const Request &req) = delete;

	void set_result(const kjson::Value &val);
	void set_exception(const kjson::Value &val);
	void set_exception(int code, const std::string_view &message);
	void set_exception(int code, const std::string_view &message, const kjson::Value &data);
	void signal_unknown_call(const std::string_view &reason);
	void set_no_result();

	kjson::Value operator[](std::size_t idx) const;
	kjson::Value operator[](std::string_view idx) const;
	kjson::Value get_args() const;


protected:

	///shared pointer to owner's node
	std::weak_ptr<Peer> _node;
	///id
	std::string _id;
    ///contains name of method
    std::string _method_name;
    ///Contains arguments
    kjson::Value _args;
    ///
    bool _response_sent;
};


class Response {
public:

	enum class ResponseType {
	    result,
	    exception,
	    method_not_found
	};

	Response(ResponseType type, kjson::Value data)
		:d(data),t(type) {}

	kjson::Value get_result() const {
		if (t == ResponseType::result) return d; else return kjson::Value();
	}
	kjson::Value get_exception() const{
		if (t == ResponseType::exception) return d; else return kjson::Value();
	}
	std::string_view get_unknown_method_reason() const{
		if (t == ResponseType::method_not_found) return d.get_string(); else return std::string_view();
	}

	bool has_result() const {return t == ResponseType::result;}
	bool has_exception() const {return t == ResponseType::exception;}
	bool has_unknown_method_error() const {return t == ResponseType::method_not_found;}
protected:
	kjson::Value d;
	ResponseType t;
};


using ResponseCallback = ondra_shared::Callback<void(const Response &)>;


}



#endif /* LIB_UMQ_REQUEST_H_qwpodj023jd9d928dw */
