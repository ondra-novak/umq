#ifndef LIB_UMQ_REQUEST_H_qwpodj023jd9d928dw
#define LIB_UMQ_REQUEST_H_qwpodj023jd9d928dw
#include <memory>
#include <string_view>
#include <shared/callback.h>

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

using ResponseCallback = ondra_shared::Callback<void(const Response &)>;

class Request {
public:
    Request(const PWkPeer &node,
            const std::string_view &id,
            const std::string_view &method_name,
            const std::string_view &data);

    ~Request();

    Request (const Request &req) = delete;
    Request (Request &&req);
    Request &operator=(const Request &req) = delete;

    ///Send result and finish the request
    void send_result(const std::string_view &val);
    ///Send request as the result
    /**
     * By sending request, other side receives request to send additional informations,
     * (reverses request)
     * @param data data associated with the request
     * @param cb callback function called when result is received
     */
    void send_request(const std::string_view &data, ResponseCallback &&cb);
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
    ///Send information
    /**
     * Information frame contains additional information about processing the request.
     * The request is not considered as finished
     *
     * @param text content of frame
     */
    void send_info(const std::string_view &text);

    ///Retrieve data of the request
    const std::string &get_data() const;


protected:

    ///shared pointer to owner's node
    std::weak_ptr<Peer> _node;
    ///id
    std::string _id;
    ///contains name of method
    std::string _method_name;
    ///Contains arguments
    std::string _args;
    ///
    bool _response_sent;
};


class Response {
public:

    enum class Type {
        ///response contains a valid result
        result,
        ///response contains request, which must be responsed to continue
        request,
        ///response contains exception thrown from method
        exception,
        ///response contains reason, why method cannot be executed
        execute_error,
        ///response is empty, request was not processed because peer is disconnected
        disconnected,
        ///response is only information, not actual response. The request continues in processing
        information,

    };

    Response(Type type, const std::string_view &data)
        :_t(type),_d(data) {}

    Response(const std::string_view &data, std::unique_ptr<Request> &&req)
        :_t(Type::request),_d(data),_req(std::move(req)) {}

    Response(Response &&) = default;
    Response(const Response &) = delete;

    const std::string &get_data() const {
        return _d;
    }

    ///Retrieve the request to be replied
    Request &get_request() {
        return *_req;
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
    bool is_request() const {return _t == Type::request;}
    bool is_information() const {return _t == Type::information;}
protected:
    Type _t;
    std::string _d;
    std::unique_ptr<Request> _req;
};











}



#endif /* LIB_UMQ_REQUEST_H_qwpodj023jd9d928dw */
