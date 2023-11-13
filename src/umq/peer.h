/*
 * peer.h
 *
 *  Created on: 3. 11. 2023
 *      Author: ondra
 */

#ifndef SRC_UMQ_PEER_H_
#define SRC_UMQ_PEER_H_
#include "connection.h"

#include "peer_config.h"

#include <functional>
#include <memory>
#include <optional>



namespace umq {

class Peer  {
public:

    static constexpr unsigned int version = 1;

    class Error: public std::exception {
    public:
        Error(std::string msg):msg(std::move(msg)) {}
        Error(std::string_view msg):msg(msg) {}
        const char *what() const noexcept override {return msg.c_str();}
        unsigned int get_code() const;
        std::string_view get_message() const;
    protected:
        std::string msg;
    };

    class SubscriptionClosed: public std::exception {
    public:
        const char *what() const noexcept override {return "Subscription closed";}
    };

    ///Error - peer rejected the connection
    static constexpr unsigned int err_rejected = 1;
    ///Callback ID was not found
    static constexpr unsigned int err_callback_not_found = 2;
    ///Malformed message
    static constexpr unsigned int err_protocol_error = 3;
    ///Requested command, which is not supported
    static constexpr unsigned int err_unsupported_command = 4;
    ///Requested version which is not supported
    static constexpr unsigned int err_unsupported_version = 5;
    ///Received RPC request, but nobody is able to process such a request (RPC error)
    static constexpr unsigned int err_no_rpc = 6;
    ///Received RPC request, but nobody is able to process such a request (RPC error)
    static constexpr unsigned int err_rpc_route_error = 7;
    ///Received RPC request, but nobody is able to process such a request (RPC error)
    static constexpr unsigned int err_rpc_temporary_unavailable= 8;

    static std::string_view errorMessage(unsigned int error);
    static auto format_error(unsigned int err) {
        return [err](auto &s) {s << err << " " << errorMessage(err);};
    }

    Peer() = default;
    ~Peer();

    using BinaryPayload = std::vector<unsigned char>;
    using Attachments = std::vector<SharedFuture<BinaryPayload> >;
    using ID = std::uint32_t;

    struct Payload {
        ///message identifier (used as version for handshake)
        ID id;
        ///text payload
        std::string text;
        ///attachments
        Attachments attachments;

        Payload() = default;
        Payload(ID id, const std::string_view &str, Attachments &&att)
            :id(id), text(str), attachments(att) {}

    };



    ///Starts client connection
    /**
     * @param conn a connection which will be associated with the peer
     * @param message hello message (optional)
     * @param attachments hello message binary attachments (optional)
     * @return future with server response whether connection has been accepted or not
     */
    Future<Payload> start_client(std::unique_ptr<IConnection> conn, const std::string_view &message = {}, Attachments &&attachments = {});


    ///Starts server connection
    /**
     * Associates connection with the object and awaits for hello message. Once message
     * arrives, the server must inspect the message and call accept_client() to accept the
     * client, or reject_client() to reject the client.
     *
     * @param conn connection
     * @return hello message
     */
    Future<Payload> start_server(std::unique_ptr<IConnection> conn);

    ///Accepts client
    /**
     * Function must be called after start_server() otherwise it is invalid
     *
     * @param message message sent with the response
     * @param attachments attachments (optional)
     */
    void accept_client(const std::string_view &message = {}, Attachments &&attachments = {});

    ///Reject client
    /**
     * Function must be called after start_server() otherwise it is invalid
     *
     * @param message message sent with the response
     * @param attachments attachments (optional)
     * @note function automatically closes the connection
     */
    void reject_client(const std::string_view &message = {});


    ///Initiates waiting on close event
    /**
     * @return a future which is resolved, when close signal is received
     * @note there can be only one active future. Previously active future is
     * finished with broker promise error. You can convert the future into shared_future
     */
    Future<void> close_event();


    ///Perform RPC call
    /**
     * @param message a message. The format of the message and arguments is not
     * specified here, it depends on how the server processes the message. It could
     * be for instance a JSONRPC message
     * @param attachments binary attachments associated with the call
     * @return response as a payload
     */
    Future<Payload> rpc_call(const std::string_view &message, Attachments &&attachments = {});


    ///Run RPC server
    /**
     * Awaits for single RPC message, returns it as result. You need to call this function
     * again to awaits for next message. There can be only one RPC server.
     * @return message as payload
     *
     * @note Synchronous access to result is discouraged unless the RPC communication
     * is strictly synchronous, meaning it is "ping pong" communation. Otherwise
     * you can miss requests
     *
     * @note there can be only one RPC server at time. Operation is not MT Safe
     */
    Future<Payload> rpc_server();


    ///Send RPC result
    /**
     * @param id id of the message from the request
     * @param response respone data
     * @param attachments attachments
     */
    void rpc_result(ID id, const std::string_view &response, Attachments &&attachments = {});

    ///Send RPC exception
    /**
     * @param id id of the message from the request
     * @param response respone data
     */
    void rpc_exception(ID id, const std::string_view &message);


    ///Subscribe for receive a stream of messages
    /**
     * @return ID of the new subscription.
     * @note function just reserves the new ID. You need to use RPC to request
     * subscription on the server.
     *
     */
    ID create_subscription();


    ///Listen on subscription
    /**
     * You need to start listening the subscription before it is requested on
     * publisher, otherwise subscription can be ended prematurely (without notification)
     *
     * @param subscription identifier
     * @return received data
     *
     * @note to continue listening on subscription, you need to call this function
     * again. This must be done in coroutine or in callback, synchronous waiting
     * is discouraged. Failing doing this causes unsubscribe
     *
     * @note to unsubscribe, simply stop calling this function
     */
    Future<Payload> listen_subscription(ID subscription);

    struct Core;

    ///Represents single subscription created on publisher's peer node
    /**
     * The structure is returned from function begin_publish and represents
     * opened subscription
     */
    class Subscription {
        ///Weak pointer to internal structure
        std::weak_ptr<Peer::Core> _target;
        ID _id;
    public:
        Subscription(std::shared_ptr<Peer::Core> target, ID id);
        ///Check state of subscription
        /**
         * @retval true subscription is active
         * @retval false subscription has ended
         */
        bool check() const;
        ///Publish to subscription
        /**
         * @param data data to publis
         * @param attachments attachments
         * @retval true sent to publish
         * @retval false subscription has ended, peer is disconected, etc, data was not
         * sent
         */
        bool publish(const std::string_view &data, Attachments &&attachments = {});
        ///Register a function which is called when subscription is ended
        /**
         * @param fn function
         * @retval true registered
         * @retval false subscription has ended (function is not called)
         */
        bool on_unsubscribe(std::function<void()> fn);
        ///Close subscription from publisher's side
        void close();
        ///Retrieves ID of the subscription
        ID get_id() const;
        ///Retrieve shared instance of the Peer;
        /**
         * @return shared insance of the Peer - can return empty value if the peer
         * is no longer available
         */
        std::optional<Peer> get_peer() const;

        bool operator<(const Subscription &x) const {
            return (_id < x._id)| ((_id == x._id) & _target.owner_before(x._target));
        }
        bool operator>(const Subscription &x) const {
            return (_id > x._id) | ((_id == x._id) & x._target.owner_before(_target));
        }
        bool operator==(const Subscription &x) const {
            return !operator!=(x);
        }
        bool operator!=(const Subscription &x) const {
            return operator>(x) | !operator<(x);
        }
        bool operator<=(const Subscription &x) const {
            return !operator>(x);
        }
        bool operator>=(const Subscription &x) const {
            return !operator<(x);
        }

    };

    ///Starts publish to subscription. This is called on publisher's side
    /**
     * @param subscription ID of subscription (received from subscriber)
     * @return returns future which becomes resolved when subscription is closed. You
     * should use a callback asynchronously process the event about closing. This
     * event is send regardless on,which side closed the subscription.
     */
    Subscription begin_publish(ID subscription);


    ///Result of callback call
    /** It has base of Payload, however adds promise which must be resolved
     * with result
     */
    struct CallbackCall: Payload {

        struct Result {
            std::string_view _text;
            Attachments _attachments;
            Result(const std::string_view &text):_text(text) {}
            Result(const std::string_view &text, Attachments &&att):_text(text), _attachments(std::move(att)) {}
        };
        ///Promise must be resolved with result of the call
        /**
         * This can be called as a function with one or two arguments, same
         * arguments as constructor of Result.
         *
         * You can also call .reject() to emit an exception
         */
        Future<Result>::Promise respond;

        CallbackCall(ID id, const std::string_view &str, Attachments &&att, Future<Result>::Promise &&respond)
            :Payload(id, str, std::move(att))
            ,respond(std::move(respond)) {}
    };

    ///RPC callback
    /** RPC callback is a rpc call from the server to the client. It is one-shot call.
     */
    struct Callback {
        ///Identifier of this callback, you need to send this ID to the server in a response
        /** The server will use this ID to call your callback */
        ID id;
        /// Future which is resolved once the call is made
        Future<CallbackCall> result;
    };


    ///Create a one-shot callback call
    /**
     * @return information of callback call. You receive a future which can be used
     * to await the callback call
     */
    Callback create_callback_call();


    ///Cancel callback call.
    /**
     * Destroys promise for given callback. Attempt to await associated future throws
     * error BrokenPromise
     *
     * @param callback_id id of callback to cancel
     */
    void cancel_callback_call(ID callback_id);

    ///Calls RPC callback
    /**
     * @param id ID of the callback (received from the client)
     * @param message message payload
     * @param attachments attachments
     * @return result of callback function
     */
    Future<Payload> rpc_callback_call(ID id, const std::string_view &message, Attachments &&attachments = {});


    ///Waits until all data are flushed.
    Future<bool> flush();

    ///Close connection
    /**
     * Sends close request to other side. You can still receive messages,
     * however new messages can't be send.
     * Connection is closed when close_event() is signaled
     */
    void close();

    ///Immediately closes connection
    void shutdown();

    ///Sets attribute on remote peer
    /**
     * @param attribute_name attribute name
     * @param attribute_value attribute value
     * @param attachments attributes can have attachments
     *
     * Attributes are updated asynchronously, however, they are visible for
     * messages sent after they are set
     */
    void set_attribute(const std::string_view &attribute_name, const std::string_view &attribute_value, Attachments &&attachments = {});

    ///Clears attribute
    /**
     * @param attribute_name name of attribute
     */
    void clear_attribute(const std::string_view &attribute_name);
    ///Retrieve attribute set by remote peer
    /**
     * @param attribute_name name of an attribute
     * @return value or undefined
     */
    std::optional<Payload> get_attribute(const std::string_view &attribute_name) const;


public:
    static constexpr char cmd_attachment = 'A';
    static constexpr char cmd_attachment_error = '-';
    static constexpr char cmd_hello = 'H';
    static constexpr char cmd_welcome = 'W';
    static constexpr char cmd_fatal_error= 'F';
    static constexpr char cmd_rpc_call= 'C';
    static constexpr char cmd_callback_call= 'B';
    static constexpr char cmd_rpc_result= 'R';
    static constexpr char cmd_rpc_exception= 'E';
    static constexpr char cmd_rpc_error= '!';
    static constexpr char cmd_topic_update= 'T';
    static constexpr char cmd_topic_close= 'D';
    static constexpr char cmd_topic_unsubscribe= 'U';
    static constexpr char cmd_attribute_set='S';
    static constexpr char cmd_attribute_reset='X';





protected:

    struct Sender;
    struct Receiver;
    struct PendingCallback;
    struct UnsubscribeNotify;

    std::shared_ptr<Core> _core;


    Peer(std::shared_ptr<Core> c): _core(std::move(c)) {}




    static void toBase36(ID id, std::ostream &out);
    static ID fromBase36(std::string_view txt);




};


}



#endif /* SRC_UMQ_PEER_H_ */
