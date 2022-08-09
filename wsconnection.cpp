#include "wsconnection.h"

#include "message.h"
namespace umq {

WSConnection::WSConnection(userver::WSStream &&stream)
    :_s(std::move(stream))
{

}


void WSConnection::start_listen(umq::ConnectionListener &&listener) {
    using namespace userver;
    _s.recv_loop() >> [=, listener = std::move(listener)](const userver::WSMessage &msg) {
          switch(msg.type) {
              case WSFrameType::connClose:
                  listener({});
                  break;
              case WSFrameType::binary:
                  listener(MessageRef{MessageType::binary, msg.data});
                  break;
              case WSFrameType::text:
                  listener(MessageRef{MessageType::text, msg.data});
                  break;
              default:
                  break;
          }
          return true;
    };
}

void WSConnection::flush() {
    _s.flush(); // @suppress("Return value not evaluated")
}

bool WSConnection::send_message(const umq::MessageRef &msg) {
    switch(msg.type) {
        case MessageType::text:
            return _s.send_text(msg.data);

        case MessageType::binary:
            return _s.send_binary(msg.data);

        default:
            return false;
    }
}


bool WSConnection::is_hwm(std::size_t v) {
    return _s.get_buffered_amount() > v;
}

}
