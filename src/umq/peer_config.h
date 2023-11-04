/*
 * peer_config.h
 *
 *  Created on: 4. 11. 2023
 *      Author: ondra
 */

#ifndef SRC_UMQ_PEER_CONFIG_H_
#define SRC_UMQ_PEER_CONFIG_H_
#include <limits>

namespace umq {


struct PeerConfig {

    std::size_t max_text_message_size = std::numeric_limits<std::size_t>::max();
    std::size_t max_binary_message_size = std::numeric_limits<std::size_t>::max();

    std::size_t max_rpc_size = std::numeric_limits<std::size_t>::max();
    std::size_t max_rpc_attachment_size = std::numeric_limits<std::size_t>::max();
    std::size_t max_rpc_attachment_count = std::numeric_limits<std::size_t>::max();

    std::size_t max_topic_size = std::numeric_limits<std::size_t>::max();
    std::size_t max_topic_attachment_size = std::numeric_limits<std::size_t>::max();
    std::size_t max_topic_attachment_count = std::numeric_limits<std::size_t>::max();

    std::size_t max_attribute_size = std::numeric_limits<std::size_t>::max();
    std::size_t max_attribute_attachment_size = std::numeric_limits<std::size_t>::max();
    std::size_t max_attribute_attachment_count = std::numeric_limits<std::size_t>::max();

};

}




#endif /* SRC_UMQ_PEER_CONFIG_H_ */
