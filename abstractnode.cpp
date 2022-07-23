/*
 * abstractnode.cpp
 *
 *  Created on: 23. 7. 2022
 *      Author: ondra
 */

#include "abstractnode.h"

#include <kissjson/parser.h>
namespace umq {

std::string_view AbstractNode::version = "1.0.0";

AbstractNode::AbstractNode(std::unique_ptr<AbstractConnection> &&conn)
        :_conn(std::move(conn)), _enc(kjson::OutputType::ascii)
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
    if (msg.type == MessageType::binary) {
        if (!on_binary_message(msg)) {
            send_node_error(NodeError::unexpectedBinaryFrame);
        }
    } else {
        try {
            auto v = kjson::Value::from_string(msg.data);
            if (v.is_array() && !v.empty()) {
                auto t = v[0];
                if (t.is_string()) {
                    auto strt = t.get_string();
                    kjson::Value v1, v2;
                    if (!strt.empty())  {
                        char flag = strt[0];
                        auto id = strt.substr(1);
                        try {
                            switch (flag) {
                                default:
                                    send_node_error(NodeError::unknownMessageType);
                                    break;
                                case 'C': v1 = v[1];
                                          v2 = v[2];
                                          if (v1.is_string()) {
                                              auto m = v1.get_string();
                                              try {
                                                  if (!on_call(id, m, v2)) {
                                                      send_unknown_method(id, m);
                                                  }
                                              } catch (std::exception &e) {
                                                  send_exception(id, static_cast<int>(NodeError::unhandledException),e.what());
                                              }
                                          } else {
                                              send_node_error(NodeError::invalidMesssgeFormat_Call);
                                          }
                                          break;
                                case 'R': v1 = v[1];
                                          if (v1.defined()) {
                                            on_result(id, v1);
                                          } else {
                                            send_node_error(NodeError::invalidMessageFormat_Result);
                                          }
                                          break;
                                case 'E': v1 = v[1];
                                          if (v1.defined()) {
                                            on_exception(id, v1);
                                          } else {
                                            send_node_error(NodeError::invalidMessageFormat_Exception);
                                          }
                                          break;
                                case '?': v1 = v[1];
                                          if (v1.is_string()) {
                                            on_unknown_method(id, v1.get_string());
                                          } else {
                                            send_node_error(NodeError::invalidMessageFormat_UnknownMethod);
                                          }
                                          break;
                                case 'T': v1 = v[1];
                                          if (v1.defined()) {
                                            if (!on_topic_update(id, v[1])) {
                                                    send_unsubscribe(id);
                                            }
                                          } else {
                                              send_node_error(NodeError::invalidMessageFormat_TopicUpdate);
                                          }
                                          break;
                                case 'U': on_unsubscribe(id);
                                          break;
                                case 'N': on_topic_close(id);
                                          break;
                                case 'S': on_set_var(id, v[1]);
                                          break;
                                case 'H': v1 = v[1];
                                          v2 = v[2];
                                          if (v1.get_string() != version) {
                                              send_node_error(NodeError::unsupportedVersion);
                                          } else {
                                             v1 = on_hello(v1.get_string(), v2);
                                             send_welcome(version, v1);
                                          }
                                          break;
                                case 'W': v1 = v[1];
                                          v2 = v[2];
                                          if (v1.get_string() != version) {
                                              send_node_error(NodeError::unsupportedVersion);
                                          } else {
                                              on_welcome(v1.get_string(), v2);
                                          }
                                          break;
                                }
                        } catch (const std::exception &e) {
                            send_node_error(NodeError::messageProcessingError);
                        }
                    }
                }
            }

            send_node_error(NodeError::invalidMessageFormat);

        } catch (const kjson::ParseError &err) {
            send_node_error(NodeError::messageParseError);
        }
    }
}



void AbstractNode::send_call(const std::string_view &id,
        const std::string_view &method, const kjson::Value &args) {
    _conn->send_message(prepareMessage('C', id, {method, args}));
}

bool AbstractNode::send_topic_update(const std::string_view &topic_id, const kjson::Value &data) {
    _conn->send_message(prepareMessage1('T', topic_id, data));
    return true;
}

void AbstractNode::send_topic_close(const std::string_view &topic_id) {
    _conn->send_message(prepareMessage('N', topic_id));
}

void AbstractNode::send_unsubscribe(const std::string_view &topic_id) {
    _conn->send_message(prepareMessage('U', topic_id));
}

void AbstractNode::send_result(const std::string_view &id,
        const kjson::Value &data) {
    _conn->send_message(prepareMessage1('R', id, data));
}

void AbstractNode::send_exception(const std::string_view &id,
        const kjson::Value &data) {
    _conn->send_message(prepareMessage1('E', id, data));
}


void AbstractNode::send_exception(const std::string_view &id, int code,
        const std::string_view &message) {
    send_exception("", {
            {"code", code},
            {"message", message}
    });
}

void AbstractNode::send_unknown_method(const std::string_view &id,
        const std::string_view &method_name) {
    _conn->send_message(prepareMessage1('?', id, method_name));
}

void AbstractNode::send_welcome(const std::string_view &version,
        const kjson::Value &data) {
    _conn->send_message(prepareMessage('W', "", {version,data}));
}

void AbstractNode::send_hello(const std::string_view &version,const kjson::Value &data) {
    _conn->send_message(prepareMessage('H', "", {version,data}));
}

void AbstractNode::send_hello(const kjson::Value &data) {
    _conn->send_message(prepareMessage('W', "", {version,data}));
}

void AbstractNode::send_var_set(const std::string_view &variable,const kjson::Value &data) {
    _conn->send_message(prepareMessage1('S', variable, data));
}

void AbstractNode::stop() {
    _conn->stop_listen();
}


std::string AbstractNode::prepareHdr(char type, const std::string_view &id) {
    return std::string(&type,1).append(id);
}

Message AbstractNode::prepareMessage(char type, std::string_view id,kjson::Array data) {
    return Message{MessageType::text, kjson::Value::concat({kjson::Array{prepareHdr(type, id)},data}).to_string()};
}

Message AbstractNode::prepareMessage1(char type, std::string_view id, kjson::Value data) {
    return Message{MessageType::text, kjson::Value(kjson::Array{kjson::Value(prepareHdr(type, id)),data}).to_string()};
}

void AbstractNode::set_encoding(kjson::OutputType ot) {
    _enc = ot;
}

kjson::OutputType AbstractNode::get_encoding() const {
    return _enc;
}

void AbstractNode::send_node_error(NodeError error) {
    send_exception("",static_cast<int>(error),error_to_string(error));
    _conn->disconnect();
}

const char *AbstractNode::error_to_string(NodeError err) {
    switch(err) {
    case NodeError::noError: return "No error";
    case NodeError::unexpectedBinaryFrame: return "Unexpected binary frame";
    case NodeError::messageParseError: return "Message parse error";
    case NodeError::invalidMessageFormat: return "Invalid message format";
    case NodeError::invalidMesssgeFormat_Call: return "Invalid message format - message C - Call";
    case NodeError::invalidMessageFormat_Result: return "Invalid message format - message R - Result";
    case NodeError::invalidMessageFormat_Exception: return "Invalid message format - message E - Exception";
    case NodeError::invalidMessageFormat_UnknownMethod: return "Invalid message format - message ? - Unknown method";
    case NodeError::invalidMessageFormat_TopicUpdate: return "Invalid message format - message T - Topic update";
    case NodeError::unknownMessageType: return "Unknown message type";
    case NodeError::messageProcessingError: return "Internal node error while processing a message";
    case NodeError::unsupportedVersion: return "Unsupported version";
    case NodeError::unhandledException: return "Unhandled exception";
    default: return "Undetermined error";
    }
}


Message AbstractNode::prepareMessage(char type, std::string_view id) {
    return Message{MessageType::text, kjson::Value(kjson::Array{prepareHdr(type, id)}).to_string()};
}

}


