#include "userver/websockets_server_handler.h"
#include "userver/http_server.h"

using namespace userver;

std::string_view page = R"html(<!DOCTYPE html>
<html>
<head>
<title>WebSocket test</title>
</head>
<body>
</body>
</html>
)html";


void ws_example(WSStream &stream) {
    //create as unique ptr
    auto sptr = std::make_unique<WSStream>(std::move(stream));

    //we transfer sptr, but also move stream pointer.
    //This pointer stays valid even if sptr is moved somewhere else
    //note that pointer stays valid until the loop is exited
    sptr->recv_async_loop([stream = sptr.get(), sptr = std::move(sptr)](const WSStream::Message &msg) mutable {

        if (msg.type == WSFrameType::text) {
            stream->send_text(msg.data);
        } else if (msg.type == WSFrameType::connClose) {
            stream->flush_async([sptr = std::move(sptr)](bool ok) {
                //empty, sptr will be released
            });
            return false;
        }
        return true;
    });
}

int main(int argc, char **argv) {

    auto addrs = NetAddr::fromString("*", "10000");
    HttpServer server;
    server.addPath("", [](PHttpServerRequest &req, const std::string_view &vpath) {
        if (vpath != "/") return false;
       req->setContentType("text/html;charset=utf-8");
       req->send(page);
       return true;
    });

    server.addPath("/ws", WebsocketServerHandler([](WSStream &stream){
       ws_example(stream);
    }));
    server.start(addrs, createAsyncProvider({1,4}));
    server.stopOnSignal();
    server.runAsWorker();
}

