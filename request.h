#ifndef LIB_UMQ_REQUEST_H_qwpodj023jd9d928dw
#define LIB_UMQ_REQUEST_H_qwpodj023jd9d928dw
#include <memory>
#include <string_view>
#include <shared/callback.h>
#include <vector>

namespace umq {

class Peer;

using PPeer = std::shared_ptr<Peer>;
using PWkPeer = std::weak_ptr<Peer>;


///Callback when topic update - but if data are 'undefined', then topic is closed
/** @param data - data of topic
 * @retval true continue receive topic
 * @retval false stop receive topic (unsubscribe)
 */
using TopicUpdateCallback = ondra_shared::Callback<bool(const std::string_view &data)>;


class Response;
class Request;

using ResponseCallback = ondra_shared::Callback<void(Response &&)>;



class Request {
public:

    Request(const PWkPeer &node,
            const std::string_view &id,
            const std::string_view &method_name,
            const std::string_view &data,
            bool discover_request);


    ~Request();

    Request (const Request &req) = delete;
    Request (Request &&req);
    Request &operator=(const Request &req) = delete;

    ///Send result and finish the request
    void send_result(const std::string_view &val) ;
    ///Send exception and finish the request
    /**
     * @param val excetion data
     */
    void send_exception(const std::string_view &val);
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
    void send_execute_error(const std::string_view &reason);
    ///Send empty result (same as send_result("");
    void send_empty_result();

    ///Retrieve data of the request
    std::string_view get_data() const;

    std::weak_ptr<Peer> get_peer() const {
        return _node;
    }

    ///Retrieves pointer to peer
    /** If the peer is no longer available, the exception is thrown.
     * @return
     */
    PPeer lock_peer() const {
        auto peer = _node.lock();
        if (peer) return peer;
        throw std::runtime_error("Peer no longer available");
    }

    std::string_view get_id() const {
        return _id;
    }

    std::string_view get_method_name() const {
        return _method_name;
    }

    bool is_response_sent() const {
        return _response_sent;
    }


    ///This is discover request
    /** This request was created as reaction to discover request message.
     * The caller expects the proxy will ask for methods of the peer which is proxied by
     * this proxy. Methods should never get such a request
     *
     * @retval true this is discover request.
     * @retval false this is normal request
     */
    bool is_discover_request() const {
        return _is_discover_request;
    }

protected:


    ///shared pointer to owner's node
    std::weak_ptr<Peer> _node;
    ///
    bool _response_sent;

    bool _is_discover_request = false;
    ///store all string data here - to easy move the request
    std::vector<char> _string_data;
    ///id
    std::string_view _id;
    ///contains name of method
    std::string_view _method_name;
    ///Contains arguments
    std::string_view _args;
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

    Response(Type type, const std::string_view &data)
        :_t(type),_d(data) {}

    Response(Response &&) = default;
    Response(const Response &) = delete;

    const std::string &get_data() const {
        return _d;
    }

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
    std::string _d;
    std::unique_ptr<Request> _req;
};











}



#endif /* LIB_UMQ_REQUEST_H_qwpodj023jd9d928dw */
