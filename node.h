/*
 * node.h
 *
 *  Created on: 23. 7. 2022
 *      Author: ondra
 */

#ifndef LIB_UMQ_NODE_H_32130djwoeijd08923jdeioew
#define LIB_UMQ_NODE_H_32130djwoeijd08923jdeioew
#include "abstractnode.h"
#include "methodlist.h"
#include <shared/callback.h>
#include <map>
#include <memory>
#include <shared_mutex>

namespace umq {


///Called on server when hello request arrived, can return response. From this point, node is ready
using HelloRequest = ondra_shared::Callback<kjson::Value(const kjson::Value)>;
///Called in client when welcome request arrived. From this point, node is ready
using WelcomeResponse = ondra_shared::Callback<void(const kjson::Value)>;
///Called to unsubscribe topic by subscriber
using UnsubscribeRequest = ondra_shared::Callback<void(const std::string_view &)>;
///called when node disconnects, before it is destroyed
using DisconnectEvent = ondra_shared::Callback<void(Node &nd)>;

using BinaryContentEvent = ondra_shared::Callback<void(const std::string_view &hash, const std::string_view &content)>;


class Node: protected AbstractNode, public std::enable_shared_from_this<Node> {
public:

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
    void init_client(PConnection &&conn,const kjson::Value &req,  WelcomeResponse &&resp);

    ///Perform RPC call
    /**
     *
     * @param method method
     * @param params parameters
     * @param result callback which handles result
     */
    void call(const std::string_view &method, const kjson::Value &params, ResponseCallback &&result);

    ///Subscribes given topic
    /** Doesn't perform actuall subscription, it only prepares
     * the node to receive a process given subscription. The actual
     * subscription is registered via a request. Other side must
     * respond with a topic, which must be registered immediatelly
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
     * @return function to call to publish the update for this node.
     *
     * @note unsubscribe is performed implicitly. When node unsubscribes,
     * the next call of the TopicUpdateCallback function returns false,
     * which causes, that function is removed from the topic. Note
     * that untill next call, the publisher has no information about
     * node unsubscribe, which can cause build up memory temporaryli (in
     * the case of sequence sub-unsub-sub-unsub...)
     */
    TopicUpdateCallback start_publish(const std::string_view &topic);



    ///Specifies callback function when unsubscribe is requested
    /**
     * @param topic topic name
     * @param cb callback function called when remote node wants to unsubscribe
     * @retval true function registered
     * @retval false topic is not registered, already unsubscribed, or node is down
     */
    bool on_unsubscribe(const std::string_view &topic, UnsubscribeRequest &cb);

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

    ///Called on disconnect to handle extra cleanup or try to reconnect
    void on_disconnect(DisconnectEvent &&disconnect);


    ///Sets remote variable
    /**
     * Remote variable is send to the other side to be stored. Other side will
     * see the variable as local variable
     *
     * @param name name of the variable
     * @param value content of variable
     */
    void set_remote_variable(const std::string_view &name, const kjson::Value &value);

    ///Retrieves remote variable
    /**
     * Retrieves copy of remote variable as it is backed at node.      *
     *
     * @param name name of variable
     * @return value content of variable
     */
    kjson::Value get_remote_variable(const std::string_view &name) const;

    ///Retrieves remote variables
    /**
     * Retrieves copy of remote variable as it is backed at node.      *
     *
     * @param name name of variable
     * @return value content of variable
     */
    kjson::Value get_remote_variables() const;

    ///Retrieve local variable - variable stored by remote side locally
    /**
     * @param name name of the variable.
     * @return content of the variable
     */
    kjson::Value get_local_variable(const std::string_view &name) const;


    ///Registers binary content
    /**
     * When node receives binary content, calculates a hash and finds
     * registered callback. If the callback is not registered,
     * the node rejects the content
     *
     *
     * @param hash hash of the content
     * @param event a callback function
     *
     * @node remote node must calculate hash, and include this
     * hash to a request. Once request is send, it can send
     * the binary content as well (without waiting to response).
     *
     * The local node receives request and registers the hash. It
     * must be done synchronously. Then it can wait for
     * the binary content and after it is received, the request can
     * be finished and send response to the remote node
     */
    void expect_binary(const std::string_view &hash, BinaryContentEvent &&event);

protected:

    friend class Request;
	virtual void on_result(const std::string_view &id, const kjson::Value &data);
	virtual void on_welcome(const std::string_view &version, const kjson::Value &data);
	virtual void on_exception(const std::string_view &id, const kjson::Value &data);
	virtual void on_topic_close(const std::string_view &topic_id);
	virtual kjson::Value on_hello(const std::string_view &version,
			const kjson::Value &data);
	virtual void on_unsubscribe(const std::string_view &topic_id);
	virtual bool on_topic_update(const std::string_view &topic_id,
			const kjson::Value &data);
	virtual bool on_call(const std::string_view &id, const std::string_view &method,
			const kjson::Value &args);
	virtual void on_unknown_method(const std::string_view &id, const std::string_view &method_name);
	virtual bool on_binary_message(const umq::MessageRef &msg);
	virtual void on_set_var(const std::string_view &variable, const kjson::Value &data);
protected:

    using Topics = std::map<std::string, UnsubscribeRequest, std::less<> >;
    using Subscriptions = std::map<std::string, TopicUpdateCallback, std::less<> >;
    using HashMap = std::multimap<std::string, BinaryContentEvent, std::less<> >;
    using CallMap = std::map<std::string, ResponseCallback, std::less<> >;



    PMethodList _methods;
    Topics _topic_map;
    Subscriptions _subscr_map;
    CallMap _call_map;
    HashMap _hash_map;
    HelloRequest _hello_cb;
    WelcomeResponse _welcome_cb;

    std::shared_timed_mutex _topic_lock;
    std::shared_timed_mutex _subscr_lock;
    std::mutex _hashmap_lock;
    std::mutex _callmap_lock;
    unsigned int _call_id = 0;


    void finish_call(const std::string_view &id,
    				Response::ResponseType type,
					const kjson::Value &data);

    static std::string calc_hash(const std::string_view &bin_content);

};
}



#endif /* LIB_UMQ_NODE_H_32130djwoeijd08923jdeioew */
