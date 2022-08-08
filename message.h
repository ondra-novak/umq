/*
 * message.h
 *
 *  Created on: 23. 7. 2022
 *      Author: ondra
 */

#ifndef LIB_UMQ_MESSAGE_902ioe23ier2k323edqd
#define LIB_UMQ_MESSAGE_902ioe23ier2k323edqd
#include <string>
#include <userver/helpers.h>

namespace umq {

enum class MessageType {
    text,
    binary
};

struct MessageRef {
    MessageType type;
    std::string_view data;
};

class Message {
public:
    Message(MessageType type):type(type),small(),is_small(true) {}

    ~Message() {
        if (is_small) small.~SmallVector();
        else large.~vector();
    }

    void clear() {
        if (is_small) small.clear();
        else large.clear();
    }

    void append(const std::string_view &text) {
        auto iter = text.begin();
        if (is_small) {
            while (iter != text.end()) {
                char c= *iter++;
                if (small.size() == small.capacity()) {
                    enlarge();
                    large.push_back(c);
                    break;
                } else {
                    small.push_back(c);
                }
            }
        }
        while (iter != text.end()) {
           large.push_back(*iter++);
        }
    }


    void push_back(char c) {
        if (is_small) {
            if (small.size() == small.capacity()) {
                enlarge();
                large.push_back(c);
            } else {
                small.push_back(c);
            }
        } else {
            large.push_back(c);
        }
    }

    operator const MessageRef() const {
        if (is_small) return MessageRef{type, std::string_view(small.data(), small.size())};
        else return MessageRef{type, std::string_view(large.data(), large.size())};
    }

protected:
    MessageType type;
    static constexpr std::size_t small_msg_size = 256;
    union {
        userver::SmallVector<char,small_msg_size> small;
        std::vector<char> large;
    };
    bool is_small;

    void enlarge() {
        std::vector<char> l(small.begin(), small.end());
        small.~SmallVector();
        new(&large) std::vector<char>(std::move(l));
        is_small = false;

    }
};


}




#endif /* LIB_UMQ_MESSAGE_902ioe23ier2k323edqd */
