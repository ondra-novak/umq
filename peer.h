/*
 * node.h
 *
 *  Created on: 23. 7. 2022
 *      Author: ondra
 */

#ifndef LIB_UMQ_NODE_H_32130djwoeijd08923jdeioew
#define LIB_UMQ_NODE_H_32130djwoeijd08923jdeioew

#include "peer.h"
#include "message.h"
#include "connection.h"
#include "methodlist.h"
#include <shared/callback.h>
#include <deque>
#include <map>
#include <memory>
#include <shared_mutex>
#include <any>

namespace umq {

enum class NodeError {
    noError = 0,
    unexpectedBinaryFrame,
    messageParseError,
    invalidMessageFormat,
    invalidMesssgeFormat_Call,
    invalidMessageFormat_Result,
    invalidMessageFormat_Exception,
    invalidMessageFormat_UnknownMethod,
    invalidMessageFormat_TopicUpdate,
    unknownMessageType,
    messageProcessingError,
    unsupportedVersion,
    unhandledException,
};

///Defines behavior for high water mark signal
enum class HighWaterMarkBehavior{
    ///skip topic update (this is default)
    skip,
    ///block call until HWM is dropped
    block,
    ///ignore HWM, enqueue the update to increasing buffer
    ignore,
    ///unsubscribe the topic
    unsubscribe,
    ///close the connection
    close
};


///Called on server when hello request arrived, can return response. From this point, node is ready
using HelloRequest = ondra_shared::Callback<std::string(const std::string_view)>;
///Called in client when welcome request arrived. From this point, node is ready
using WelcomeResponse = ondra_shared::Callback<void(const std::string_view)>;
///Called to unsubscribe topic by subscriber
using UnsubscribeRequest = ondra_shared::Callback<void()>;
///called when node disconnects, before it is destroyed
using DisconnectEvent = ondra_shared::Callback<void()>;

using BinaryContentEvent = ondra_shared::Callback<void(bool valid, const std::string_view &content)>;

using SharedVariables = std::map<std::string, std::string, std::less<> >;

using PeerVariables = std::map<std::string, std::any>;

class Peer: protected AbstractConnectionListener, public std::enable_shared_from_this<Peer> {
public:

    using PConnection = std::unique_ptr<AbstractConnection>;

	Peer();

	~Peer();
    ///Called on disconnect to handle extra cleanup or try to reconnect
    /**
     * You should set this handler before the peer is initialized. For
     * client peers (this is server), you can use keep_until_disconnected which
     * heavily uses weak pointers (PWkPeer) to hold reference to the
     * peer's instance without allowing it to release on disconnect.
     *
     * @param disconnect function called when peer is disconnected
     *
     * @note once the peer is disconnected, there is no way to reconnect
     * it back again. It is better to estabilish a new connection with
     * a new Peer instance.
     */
    void on_disconnect(DisconnectEvent &&disconnect);

    ///Keeps peer object referenced until it is disconnected
    /** this allows to have object initialized and active
     * until it is disconnected. Function is implemented as on_disconnect()
     * so it resets current disconnect handler.
     */
    void keep_until_disconnected();
    ///Init server.
    /**
     * The server is side, which accepted connection. This must be called by acceptor.
     * It can also initialize object before it is ready to accept connection. Once
     * servere is initialized, it starts to process messages
     *
     * @param conn connection (created by listening and accepting incoming connection)
     * @param resp handler, which process initial message exchange Hello-Welcome.
     * Once the Hello arrives, initialization is ready. Do not send messages until
     * the initialization is complete.
     */
    void init_server(PConnection &&conn, HelloRequest &&resp);

    ///Initialize client
    /**
     * The client is side, which created connection by connecting a server.
     * @param conn connection (created using connect)
     * @param req request data sent with Hellow request.
     * @param resp callback called when Welcome message arrives
     *
     * You can start use the node after Welcome arrives, never soon.
     */
    void init_client(PConnection &&conn,const std::string_view &req,  WelcomeResponse &&resp);

    ///Perform RPC call
    /**
     *
     * @param method method
     * @param params parameters
     * @param result callback which handles result
     */
    void call(const std::string_view &method, const std::string_view &params, ResponseCallback &&result);

    ///Subscribes given topic
    /** Doesn't perform actual subscription, it only prepares
     * the peer object to receive a process given subscription. The actual
     * subscription is registered via a request. Other side must
     * respond with a topic, which must be registered immediately
     * while response is being processed. Topic can't be registered
     * later in asynchronous processing, because a message for this
     * topic could be already processed and without proper registration
     * it is rejected.
     *
     * @param topic topic to register
     * @param cb callback function called for the topic update
     */
    void subscribe(const std::string_view &topic, TopicUpdateCallback &&cb);


    ///Initiates publishing (implicit unsubscribe)
    /** Prepares node to publish.
     *
     * @param topic topic to be published
     * @param hwmb defines behaviour for high water mark signal.
     * @param hwm_size defines custom HWM size for this topic. Default 0 means to use value specified by set_hwm()
     *
     * @return function to call to publish the update for this node.
     *
     * @note unsubscribe is performed implicitly. When node unsubscribes,
     * the next call of the TopicUpdateCallback function returns false,
     * which causes, that function is removed from the topic. Note
     * that untill next call, the publisher has no information about
     * node unsubscribe, which can cause build up memory temporaryli (in
     * the case of sequence sub-unsub-sub-unsub...)
     */
    TopicUpdateCallback start_publish(const std::string_view &topic, HighWaterMarkBehavior hwmb = HighWaterMarkBehavior::skip, std::size_t hwm_size = 0);


    ///Specifies callback function when unsubscribe is requested
    /**
     * @param topic topic name
     * @param cb callback function called when remote node wants to unsubscribe
     * @retval true function registered
     * @retval false topic is not registered, already unsubscribed, or peer is down
     */
    bool on_unsubscribe(const std::string_view &topic, UnsubscribeRequest &&cb);


    ///Sets method list
    /**
     * Sets object responsible to mantain list of methods for the
     * rpc-server mode. Method list can be changed anytime, and
     * changes are propagated to the node.
     *
     * @param method_list pointer to method lis
     */
    void set_methods(const PMethodList &method_list);

    ///Unsubscribe given topic
    /**
     * @param topic Explicitly unsubscribe the topic.
     *
     * @note You can unsubscribe implicitly by returning false from
     * the callback function. It is prefered way, which eliminates
     * race conditions
     */
    void unsubscribe(const std::string_view &topic);

    ///Get variable set by peer
    /**
     * Variables are set by the peer. They can be used to store
     * peer state, authorization, JWT tokens, etc. These variables
     * can be read by the other side using this function.
     *
     * Only peer can change the variable
     *
     * @param name name of the variable
     * @return Value of the variable, if not exists, value is not set
     */
    std::optional<std::string_view> get_peer_variable(const std::string_view &name) const;

    ///Get all peer variables
    SharedVariables get_peer_variables() const;

    ///Sets the variable
    /**
     * Sets a variable associated with the connection.
     * The variable is visible to the peer.
     *
     * @param name name of the variable
     * @param value content of variable
     */
    void set_variable(const std::string_view &name, const std::string_view &value);

    ///Unsets the variable
    bool unset_variable(const std::string_view &name);

    ///Sets multiple variables
    /**
     * @param variables json-object contains multiple variables
     * @param merge if true, current variables stays set, otherwise they are cleared
     */
    void set_variables(SharedVariables &&variables, bool merge = false);

    ///Retrieves variable
    /**
     * Retrieves value of a variable set by the function set_variable
     *
     * @param name name of variable
     * @return value content of variable
     */
    std::optional<std::string_view> get_variable(const std::string_view &name) const;

    ///Retrieves all variables as single json-object
    /**
     * @return value content of variable
     */
    SharedVariables get_variables() const;

    void set_local_variable(const std::string_view &name, const std::any &value);

    std::optional<std::any> get_local_variable(const std::string_view &name);

    PeerVariables get_local_variables();


    void set_hwm(std::size_t sz);

    std::size_t get_hwm() const;

    static std::size_t default_hwm;


    ///Use to send binary message
    /**
     * If you need to send a binary message and give the peer the ID of that
     * message, you need to create instance of this class.
     *
     * Then you can call get_id() to receive ID of the message. When your
     * text message is send to the peer, you can call send() to send actual
     * binary message
     *
     */
    class BinaryMessage {
    public:

        ///Construct uninitialized instance
        BinaryMessage() = default;
        ///Construct and initialize instance ready to receive the binary message to send to the specified peer
        /**
         * @param peer peer
         */
        BinaryMessage(const PPeer &peer);

        BinaryMessage(const PWkPeer &peer);

        ///You can move the object
        BinaryMessage(BinaryMessage &&other);
        ///You can assign the object
        BinaryMessage &operator=(BinaryMessage &&other);

        BinaryMessage(const BinaryMessage &other) = delete;
        BinaryMessage operator=(const BinaryMessage &other) = delete;
        ~BinaryMessage();

        ///Retrieves ID
        std::size_t get_id() const;

        ///Send the message (you can call it only once)
        void send(const std::string_view &data);
        ///Send the message (you can call it only once)
        void send(std::string &&data);

    protected:
        PWkPeer _peer;
        std::size_t _id = 0;
    };



    void binary_receive(std::size_t id, BinaryContentEvent &&callback);


    ///Determines, whether stream is still connected
    bool is_connected() const;


protected:

    friend class Request;
	void on_result(const std::string_view &id, const std::string_view &data);
	void on_welcome(const std::string_view &version, const std::string_view &data);
	void on_exception(const std::string_view &id, const std::string_view &data);
	void on_topic_close(const std::string_view &topic_id);
	std::string on_hello(const std::string_view &version, const std::string_view &data);
	void on_unsubscribe(const std::string_view &topic_id);
	bool on_topic_update(const std::string_view &topic_id,
			const std::string_view &data);
	bool on_call(const std::string_view &id, const std::string_view &method,
			const std::string_view &args);
	void on_unknown_method(const std::string_view &id, const std::string_view &method_name);
	bool on_binary_message(const umq::MessageRef &msg);
	void on_set_var(const std::string_view &variable, const std::string_view &data);
	void on_unset_var(const std::string_view &variable);
    void on_disconnect() override;
    void on_request_continue(const std::string_view &id, const std::string_view &data);
    void on_request_info(const std::string_view &id, const std::string_view &data);


    ///Parse message from connection
    void parse_message(const MessageRef &msg);

    ///Sends topic update
    /**
     * @param topic_id topic id
     * @param data data of topic
     * @retval true topic update sent
     * @retval false other side unsubscribed this topic
     *
     * @note default implementation always returns true. Extending class can implement own logic
     *
     */
    bool send_topic_update(const std::string_view &topic_id, const std::string_view &data, HighWaterMarkBehavior hwmb, std::size_t hwm_size );

    ///Close the topic
    /**
     * Sent by publisher about the topic is closed
     * @param topic_id topic id
     */
    void send_topic_close(const std::string_view &topic_id);

    ///Unsubscribe the topic
    /**
     * Sent by subscriber to prevent sending updates on given topic
     * @param topic_id
     */
    void send_unsubscribe(const std::string_view &topic_id);

    ///Sends result of RPC call
    /**
     * @param id id of request
     * @param data data of request
     *
     *
     */
    void send_result(const std::string_view &id, const std::string_view &data);

    ///Sends exception of RPC call
    /**
     * @param id id of request
     * @param data data of request
     */
    void send_exception(const std::string_view &id, const std::string_view &data);

    void send_exception(const std::string_view &id, int code, const std::string_view &message);

    void send_exception(const std::string_view &id, NodeError code, const std::string_view &message);

    ///Sends about unknown method
    /**
     * @param id id of request
     * @param method_name method name
     */
    void send_unknown_method(const std::string_view &id, const std::string_view &method_name);

    ///Sends welcome
    /**
     * @param version version (1.0,0)
     * @param data arbitrary data
     */
    void send_welcome(const std::string_view &version, const std::string_view &data);

    ///Sends hello
    /**
     * @param version version (1.0,0)
     * @param data arbitrary data
     */
    void send_hello(const std::string_view &version, const std::string_view &data);

    ///Sets remote variable
    /**
     * @param variable variable
     * @param data data of variable - use 'undefined' to unset variable
     */
    void send_var_set(const std::string_view &variable, const std::string_view &data);


    void send_var_unset(const std::string_view &variable);



    static const char *error_to_string(NodeError err);






protected:

    std::unique_ptr<AbstractConnection> _conn;

    static std::string_view version;

    using Topics = std::map<std::string, UnsubscribeRequest, std::less<> >;
    using Subscriptions = std::map<std::string, TopicUpdateCallback, std::less<> >;
    using CallMap = std::map<std::string, ResponseCallback, std::less<> >;
    using BinaryReservation = std::deque<std::optional<std::string> >;
    using BinaryCallbacks = std::map<std::size_t, BinaryContentEvent>;


    PMethodList _methods;
    Topics _topic_map;
    Subscriptions _subscr_map;
    CallMap _call_map;
    BinaryReservation _bin_res;
    BinaryCallbacks _bin_cbs;

    HelloRequest _hello_cb;
    WelcomeResponse _welcome_cb;
    DisconnectEvent _discnt_cb;
    SharedVariables _var_map;
    SharedVariables _local_var_map;
    std::size_t _hwm;

    mutable std::shared_timed_mutex _lock;
    unsigned int _call_id = 0;
    std::size_t _send_bin_order = 0;
    std::size_t _rcv_bin_order = 0;


    void finish_call(const std::string_view &id,
    				Response::ResponseType type,
					const std::string_view &data);

    static std::string calc_hash(const std::string_view &bin_content);

    void prepareMessage(Message &msg, char type, std::string_view &topic, const std::initializer_list<std::string_view> &data);


    static std::string prepareHdr(char type, const std::string_view &id);
    Message prepareMessage(char type, std::string_view id, kjson::Array data);
    Message prepareMessage1(char type, std::string_view id, std::string_view data);
    Message prepareMessage(char type, std::string_view id);

    void send_node_error(NodeError error);

    std::size_t binary_reserve_id();

    void binary_send(std::size_t id, const std::string_view &data);

    void binary_send(std::size_t id, std::string &&data);

    void binary_flush();

    class PreparedMessage: public Message {
    public:
        PreparedMessage(char type, const std::string_view &topic, const std::initializer_list<std::string_view> &data);
    };

};

}



#endif /* LIB_UMQ_NODE_H_32130djwoeijd08923jdeioew */
