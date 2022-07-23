/*
 * node.h
 *
 *  Created on: 23. 7. 2022
 *      Author: ondra
 */

#ifndef LIB_UMQ_NODE_H_32130djwoeijd08923jdeioew
#define LIB_UMQ_NODE_H_32130djwoeijd08923jdeioew
#include "abstractnode.h"
#include <shared/callback.h>
#include <memory>

namespace umq {

class Node;

using PNode = std::shared_ptr<Node>;

enum class ResponseType {
    result,
    exception,
    method_not_found
};



using ResponseCallback = ondra_shared::Callback<void(ResponseType type, kjson::Value data)>;

///Callback when topic update - but if data are 'undefined', then topic is closed
/** @param data - data of topic
 * @retval true continue receive topic
 * @retval false stop receive topic (unsubscribe)
 */
using TopicUpdateCallback = ondra_shared::Callback<bool(kjson::Value data)>;

///Request object
/** You carry the object during execution of the function. You
 * have to eventually call result function to finish the request. if the
 * request is not fulfilled, the error is send to caller
 */
struct Request {
    ///contains name of method
    /** This field is valid until the handler is exited.
     * If function continues asynchronously, accessing to this field
     * leads to undefined behavior.
     */
    std::string_view method_name;
    ///Contains arguments
    kjson::Value args;
    ///Call this function with result
    ResponseCallback result;
};

///Called when request
using RequestHandler = ondra_shared::Callback<void(Request &req)>;
///Called on server when hello request arrived, can return response. From this point, node is ready
using HelloRequest = ondra_shared::Callback<kjson::Value(const kjson::Value)>;
///Called in client when welcome request arrived. From this point, node is ready
using WelcomeResponse = ondra_shared::Callback<void(const kjson::Value)>;
///Called to unsubscribe topic by subscriber
using UnsubscribeRequest = ondra_shared::Callback<void(const std::string_view &)>;
///called when node disconnects, before it is destroyed
using DisconnectEvent = ondra_shared::Callback<void(Node &nd)>;

using BinaryContentEvent = ondra_shared::Callback<void(std::string_view &hash, std::string_view &content)>;


class Node: public AbstractNode, public std::enable_shared_from_this<Node> {
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

    ///Enables receive a topic
    /**
     * Subscribtion is dealt using RPC call. If the result of RPC call is subscription,
     * then topic id must be included in the RPC result. Function which handles this
     * result must call on_subscribe to prepare node to receive updates on the given
     * topic, otherwise no message will be accepted
     *
     * @param topic name of the topic
     * @param callback callback to be called on topic update. Callback should
     * return true to continue listening, or false to unsubscribe
     */
    void on_subscribe(const std::string_view &topic, TopicUpdateCallback &&callback);

    ///Enable topic on publisher side
    /**
     * Topic must be enable for given connection to publish data here
     *
     * @param topic topic name
     * @param unsub_req function which handle explicit unsubscribe. Can be nullptr
     *  in this case unsubscribe is handled implicitly. This meaqns, that unsubscribe
     *  is performed on next update. Implicit unsubscribe is easy to implement,
     *  but require publisher to send data quite often to cleanup all dropped
     *  subscriptiuos. Otherwise all subscriptions stays active till next update
     *
     * @return function to be registered at real publisher to receive updates on topic.     *
     *
     */
    TopicUpdateCallback enable_topic(const std::string_view &topic, UnsubscribeRequest &&unsub_req);

    ///Add RPC method
    /**
     * @param method_name name of method
     * @param handler handle of the method
     * @param once method will be registered for one call only. Usefull for pull-push when
     * pull method is once to prevent duplicate calls of the same method
     */
    void add_method(const std::string_view &method_name, RequestHandler &&handler, bool once = false);

    ///Remove RPC method
    /**
     * @param method_name method name
     */
    void remove_method(const std::string_view &method_name);

    ///Unsubscribe given topic
    /**
     * @param topic Explicitly unsubscribe the topic.
     */
    void unsubscribe(const std::string_view &topic);

    ///Called on disconnect to handle extra cleanup or try to reconnect
    void on_disconnect(DisconnectEvent &&disconnect);


    ///Sets remote variables
    /**
     * Remote variable is send to the other side to be stored. Other side will
     * see this variable as local variable.
     *
     * @param obj Variables are stored in json-object
     *
     */
    void set_remote_variables(const kjson::Object &obj);

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

    void expect_binary(const std::string_view &hash, BinaryContentEvent &&event);






protected:


};


}



#endif /* LIB_UMQ_NODE_H_32130djwoeijd08923jdeioew */
