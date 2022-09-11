#ifndef LIB_UMQ_REQUEST_H_qwpodj023jd9d928dw
#define LIB_UMQ_REQUEST_H_qwpodj023jd9d928dw
#include <memory>
#include <string_view>
#include <shared/callback.h>
#include <vector>
#include "payload.h"

namespace umq {

class Peer;


using PPeer = std::shared_ptr<Peer>;
using PWkPeer = std::weak_ptr<Peer>;


using TopicUpdateCallback = ondra_shared::Callback<bool(const Payload &data)>;


class Response;
class Request;

using ResponseCallback = ondra_shared::Callback<void(Response &&)>;


class RequestBase {
public:
    RequestBase (const PWkPeer &peer, const std::string_view &id, const std::string_view &method_name, std::size_t extra);

    RequestBase(RequestBase &&other);
    
    RequestBase(const RequestBase &other) = delete;
    RequestBase &operator=(const RequestBase &other) = delete;
    RequestBase &operator=(RequestBase &&other) = delete;
    
    ///Determines, whether response has been already sent
    bool is_response_sent() const {
        return _response_sent;
    }
    
    std::weak_ptr<Peer> get_peer() const {
        return _peer;
    }

    ///Retrieves pointer to peer
    /** If the peer is no longer available, the exception is thrown.
     * @return
     */
    PPeer lock_peer() const {
        auto peer = _peer.lock();
        if (peer) return peer;
        throw std::runtime_error("Peer no longer available");
    }
    
    std::string_view get_id() const {
        return _id;
    }
    std::string_view get_method_name() const {
        return _method_name;
    }


protected:
    PWkPeer _peer;
    std::vector<char> _text_data;
    std::string_view _id;
    std::string_view _method_name;    
    bool _response_sent;
    
};

class Request: public RequestBase {
public:

    Request(const PWkPeer &peer,
            const std::string_view &id,
            const std::string_view &method_name,
            const Payload &data);

    Request(Request &&) = default;
    
    ~Request();


    ///Send result and finish the request
    void send_result(const Payload &val) ;
    ///Send exception and finish the request
    /**
     * @param val excetion data
     */
    void send_exception(const Payload &val);
    ///Send exception and finish the request
    /**
     * @param code exception code
     * @param message exception message
     */
    void send_exception(int code, const std::string_view &message);
    ///Send error
    /**
     * This error is reserved to communicate processing and routing errors,
     * not actual error of the method. The caller gets information that
     * method is unavailable or cannot be processed now.
     *
     * @param reason reason description
     */
    void send_execute_error(const Payload &reason);
    ///Send empty result (same as send_result("");
    void send_empty_result();

    ///Retrieve data of the request
    const Payload &get_data() const;



protected:
    Payload _args;

};


class Response {
public:

    enum class Type {
        ///response contains a valid result
        result,
        ///response contains exception thrown from method
        exception,
        ///response contains reason, why method cannot be executed
        execute_error,
        ///response is empty, request was not processed because peer is disconnected
        disconnected,

    };

    Response(Type type, const Payload &data);

    Response(Response &&) = default;
    Response(const Response &) = delete;

    const Payload &get_data() const {
        return _d;
    }

    const AttachList &get_attachments() const {return _d.attachments;}

    ///Retrieve exception code and message
    /**
     * If the exception is not in proper format, you receive code 0;
     * @return code and message
     */
    std::pair<int, std::string_view> get_exception() const;


    Type get_type() const {return _t;}

    bool is_result() const {return _t == Type::result;}
    bool is_exception() const {return _t == Type::exception;}
    bool is_execute_error() const {return _t == Type::execute_error;}
    bool is_disconnected() const {return _t == Type::disconnected;}
protected:
    Type _t;
    std::vector<char> _text;
    Payload _d;

};

///Response on discover request
struct DiscoverResponse {
    ///list of methods 
    std::vector<std::string> methods;
    ///list of routes
    std::vector<std::string> routes;
    ///documentation of the method - if queried a method
    std::string doc;
    ///error string which is set when error happened
    std::string error;
    ///doc is valid (ignore methods and routes)
    bool isdoc = false;
};

///Discover request for a route/proxy
/** Discover request is much simplier. 
 * 
 */
class DiscoverRequest: public RequestBase {
public:
    
    ///Callback function
    using Callback = ondra_shared::Callback<void(const DiscoverResponse &resp)>;
    
    ///Constructor
    /**
     * @param peer associated peer - can be undefined
     * @param cb callback
     * @param method_name - whole method name, including router's prefix ("Router:method")
     */
    DiscoverRequest(const PWkPeer &peer, Callback &&cb, const std::string_view &id, const std::string_view &method_name);
    DiscoverRequest(DiscoverRequest &&other) = default;
    ~DiscoverRequest();
    
    DiscoverRequest(const DiscoverRequest &other) = delete;
    DiscoverRequest &operator=(DiscoverRequest &&) = delete;
    DiscoverRequest &operator=(const DiscoverRequest &) = delete;

    ///Send response
    void send(const DiscoverResponse &resp);


protected:
    Callback _cb;
    

};

}



#endif /* LIB_UMQ_REQUEST_H_qwpodj023jd9d928dw */
