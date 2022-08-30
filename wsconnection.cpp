#include "wsconnection.h"

#include <shared/trailer.h>
#include "message.h"
namespace umq {

WSConnection::WSConnection(userver::WSStream &&stream)
    :_s(std::move(stream))
{

}

void WSConnection::start_listen(AbstractConnectionListener &listener) {
    using namespace userver;
    _s.recv_loop() >> [=,&listener,
                       t = ondra_shared::DeferredTrailer<AbstractConnectionListener &>()]
                            (const userver::WSMessage &msg) mutable {
          switch(msg.type) {
              case WSFrameType::connClose:
                  //call the listener outside of callback function -
                  //we use preallocated trailer
                  t.push([&listener] {
                      listener.on_close();
                  });
                  return false;
                  break;
              case WSFrameType::binary:
                  listener.on_message(MessageRef{MessageType::binary, msg.data});
                  break;
              case WSFrameType::text:
                  listener.on_message(MessageRef{MessageType::text, msg.data});
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
