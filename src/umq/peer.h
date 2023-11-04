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
    static constexpr unsigned int err_callback_not_found = 2;
    static constexpr unsigned int err_protocol_error = 3;
    static constexpr unsigned int err_unsupported_command = 4;
    static constexpr unsigned int err_unsupported_version = 5;

    static std::string_view errorMessage(unsigned int error);

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
     * finished with broker promise error
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
     * @note You should call this function inside of coroutine or use a callback to
     * receive the request. When request is received, it is required to call this function
     * again in the context of the callback, otherwise the RPC server can be
     * temporarily disabled
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


    ///Receive data send by publisher
    /**
     * @param subscription identifier
     * @return received data
     */
    Future<Payload> receive(ID subscription);

    ///Starts publish to subscription. This is called on publisher's side
    /**
     * @param subscription ID of subscription (received from subscriber)
     * @return returns future which becomes resolved when subscription is closed. You
     * should use a callback asynchronously process the event about closing. This
     * event is send regardless on,which side closed the subscription.
     */
    Future<void> begin_publish(ID subscription);

    ///Send a data to the subscription
    /**
     * @param subscription ID of subscription. Subscription must be started using begin_publish()
     * @param data data being publish
     * @param attachments
     * @return
     */
    bool publish(ID subscription, const std::string_view &data, Attachments &&attachments = {});

    ///Finishes publishing (at publisher side)
    /**
     * You need to call this function to close stream from publisher side. For instance
     * if stream ends, no more data can be published.
     *
     * @param subscription ID
     */
    void end_publish(ID subscription);

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

    void set_attribute(const std::string_view &attribute_name, const std::string_view &attribute_value, Attachments &&attachments = {});
    void unset_attribute(const std::string_view &attribute_name);
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
    static constexpr char cmd_topic_update= 'T';
    static constexpr char cmd_topic_close= 'D';
    static constexpr char cmd_topic_unsubscribe= 'U';
    static constexpr char cmd_attribute_set='S';
    static constexpr char cmd_attribute_reset='X';





protected:

    struct Core;
    struct Sender;
    struct Receiver;
    struct PendingCallback;

    std::shared_ptr<Core> _core;






    static void toBase36(ID id, std::ostream &out);
    static ID fromBase36(std::string_view txt);




};


}



#endif /* SRC_UMQ_PEER_H_ */
