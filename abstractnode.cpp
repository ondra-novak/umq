/*
 * abstractnode.cpp
 *
 *  Created on: 23. 7. 2022
 *      Author: ondra
 */

#include "abstractnode.h"

namespace umq {

AbstractNode::AbstractNode(std::unique_ptr<AbstractConnection> &&conn)
        :_conn(std::move(conn))
{
    _conn->start_listen(this);
}

AbstractNode::~AbstractNode() {
    AbstractNode::stop();
}

AbstractConnection& AbstractNode::get_connection() {
    return *_conn;
}


void AbstractNode::parse_message(const MessageRef &msg) {
}

void AbstractNode::send_call(const std::string_view &id,
        const std::string_view &method, const kjson::Value &args) {
}

bool AbstractNode::send_topic_update(const std::string_view &topic_id,
        const kjson::Value &data) {
}

void AbstractNode::send_topic_close(const std::string_view &topic_id) {
}

void AbstractNode::send_unsubscribe(const std::string_view &topic_id) {
}

void AbstractNode::send_result(const std::string_view &id,
        const kjson::Value &data) {
}

void AbstractNode::send_exception(const std::string_view &id,
        const kjson::Value &data) {
}

void AbstractNode::send_unknown_method(const std::string_view &id,
        const std::string_view &method_name) {
}

void AbstractNode::send_welcome(const std::string_view &version,
        const kjson::Value &data) {
}

void AbstractNode::send_hello(const std::string_view &version,
        const kjson::Value &data) {
}

void AbstractNode::send_hello(const kjson::Value &data) {
}

void AbstractNode::send_var_set(const std::string_view &variable,
        const kjson::Value &data) {
}

void AbstractNode::stop() {
    _conn->stop_listen();
}

}


