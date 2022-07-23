/*
 * node.h
 *
 *  Created on: 23. 7. 2022
 *      Author: ondra
 */

#ifndef LIB_UMQ_ABSTRACTNODE_H_093ujdoiewd2389edhiuw
#define LIB_UMQ_ABSTRACTNODE_H_093ujdoiewd2389edhiuw

#include <kissjson/value.h>
#include <string_view>

#include "message.h"

#include "connection.h"

#include <unordered_set>

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


class AbstractNode: protected AbstractConnectionListener {
public:

    using PConnection = std::unique_ptr<AbstractConnection>;


    AbstractNode();

    virtual ~AbstractNode();

    ///Gets associated connection
    virtual AbstractConnection &get_connection();

    virtual void set_connection(PConnection &&conn);

    ///handle call request
    /**
     * @param id request ID
     * @param method method to call
     * @param args arguments
     * @retval true, method has been processed or will be processed. It is mandatory to call
     *  the function send_result(), send_exception() or send_unknown_method() to finalize the
     *  call, otherwise caller can stuck waiting on response. Returning the true doesn't
     *  invoke any of these functions, it just tells node, that message was accepted
     * @retval false, method was not found, in this case, the node automatically responds
     *  with send_unknown_method()
     * @exception std::exception any exception produced by this function is automatically send
     * to other side using send_exception(), where what() is used as description
     *
     */
    virtual bool on_call(const std::string_view &id,
            const std::string_view &method,
            const kjson::Value &args) = 0;

    ///handle topic update
    /**
     * @param topic_id topic id
     * @param data data of topic
     * @retval true message was accepted
     * @retval false message rejected, in this case, send_unsubscribe() is automatically called
     */
    virtual bool on_topic_update(const std::string_view &topic_id, const kjson::Value &data) = 0;

    ///Notifies that topic was closed, no more message will arive
    /**
     * @param topic_id topic to be closed
     */
    virtual void on_topic_close(const std::string_view &topic_id) = 0;

    ///Notifies, that subscriber (other side) wants to unsubscribe specified topic
    /**
     * @param topic_id topic to unsubscribe
     */
    virtual void on_unsubscribe(const std::string_view &topic_id) = 0;

    ///Notifies, that result arrived to given opened call
    /**
     * @param id opened call
     * @param data data of result
     */
    virtual void on_result(const std::string_view &id, const kjson::Value &data) = 0;

    ///Notifies, that exception arrived to given opened call
    /**
     * @param id opened call
     * @param data data of result
     */
    virtual void on_exception(const std::string_view &id, const kjson::Value &data) = 0;
    ///Notifies, that requested method was not found
    /**
     * @param id id of request
     * @param method_name method name
     */
    virtual void on_unknown_method(const std::string_view &id, const std::string_view &method_name) = 0;

    ///Notifies, that other side accepted connection and welcomes you.
    /**
     * @param version version of protocol (1.0.0)
     * @param data arbitrary data
     */
    virtual void on_welcome(const std::string_view &version, const kjson::Value &data) = 0;

    ///Notifies, that other side want to (re)establish connection
    /**
     * @param version version of the protocol (1.0,0)
     * @param data data associated with the connection
     * @return data which will be send with welcome packet.
     * @exception std::exception Exception causes closing connection sending what() as reason
     */
    virtual kjson::Value on_hello(const std::string_view &version, const kjson::Value &data) = 0;

    ///Notifies, that binary packet arrived
    /**
     * @param msg binary message
     * @retval true, packet has been accepted
     * @retval false, packet has not been accepted, which causes closing connections,
     * because binary packets are not numbered, this causes, that stream is probably
     * out of sync
     */
    virtual bool on_binary_message(const MessageRef &msg) = 0;

    ///Arrives other node configuration
    virtual void on_set_var(const std::string_view &variable, const kjson::Value &data) = 0;

    ///Called when connection switches to disconnected mode
    /** in this mode no message can arrive and no message can be send, only way to continue is to
     * destroy the node, or reconnect the node.
     */
    virtual void on_disconnect() =0;

    ///Parse message from connection
    virtual void parse_message(const MessageRef &msg);

    ///Perform RPC
    /**
     * @param id arbitrary request id (should be unique)
     * @param method method to call
     * @param args arguments
     *
     * @note result arrives through on_result, on_exception, on_unknown_method
     */
    virtual void send_call(const std::string_view &id,
            const std::string_view &method,
            const kjson::Value &args);

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
    virtual bool send_topic_update(const std::string_view &topic_id, const kjson::Value &data);

    ///Close the topic
    /**
     * Sent by publisher about the topic is closed
     * @param topic_id topic id
     */
    virtual void send_topic_close(const std::string_view &topic_id);

    ///Unsubscribe the topic
    /**
     * Sent by subscriber to prevent sending updates on given topic
     * @param topic_id
     */
    virtual void send_unsubscribe(const std::string_view &topic_id);

    ///Sends result of RPC call
    /**
     * @param id id of request
     * @param data data of request
     *
     *
     */
    virtual void send_result(const std::string_view &id, const kjson::Value &data);

    ///Sends exception of RPC call
    /**
     * @param id id of request
     * @param data data of request
     */
    virtual void send_exception(const std::string_view &id, const kjson::Value &data);

    virtual void send_exception(const std::string_view &id, int code, const std::string_view &message);

    ///Sends about unknown method
    /**
     * @param id id of request
     * @param method_name method name
     */
    virtual void send_unknown_method(const std::string_view &id, const std::string_view &method_name);

    ///Sends welcome
    /**
     * @param version version (1.0,0)
     * @param data arbitrary data
     */
    virtual void send_welcome(const std::string_view &version, const kjson::Value &data);

    ///Sends hello
    /**
     * @param version version (1.0,0)
     * @param data arbitrary data
     */
    virtual void send_hello(const std::string_view &version, const kjson::Value &data);

    ///Sends hello with default version
    /**
     * @param data arbitrary data
     */
    virtual void send_hello( const kjson::Value &data);

    ///Sets remote variable
    /**
     * @param variable variable
     * @param data data of variable - use 'undefined' to unset variable
     */
    virtual void send_var_set(const std::string_view &variable, const kjson::Value &data);


    ///stop receiving messages, call this in destructor
    /**
     * It is not error to call this multiple times, just first call stops the
     * processing and other will do nothing.
     */
    virtual void stop();

    void set_encoding(kjson::OutputType ot);

    kjson::OutputType get_encoding() const;

    static const char *error_to_string(NodeError err);

protected:
    std::unique_ptr<AbstractConnection> _conn;
    kjson::OutputType _enc;

    void send_node_error(NodeError error);

    static std::string_view version;

    static std::string prepareHdr(char type, const std::string_view &id);
    Message prepareMessage(char type, std::string_view id, kjson::Array data);
    Message prepareMessage1(char type, std::string_view id, kjson::Value data);
    Message prepareMessage(char type, std::string_view id);



};


}



#endif /* LIB_UMQ_ABSTRACTNODE_H_093ujdoiewd2389edhiuw */
