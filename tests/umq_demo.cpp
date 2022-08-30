#include <userver/scheduler.h>
#include <userver/static_webserver.h>
#include "userver/websockets_server_handler.h"
#include "userver/http_server.h"

#include "../peer.h"
#include "../publisher.h"
#include "../wsconnection.h"

void counter(umq::Publisher &pub, int val) {
    using namespace userver;
    After(std::chrono::seconds(1)) >> [&, val]{
      pub.publish(std::to_string(val));
      counter(pub, val+1);
    };
}


int main(int argc, char **argv) {

    using namespace userver;


    auto addrs = NetAddr::fromString("*", "10000");
    HttpServer server;

    umq::Publisher pub_chat;
    umq::Publisher pub_counter;

    auto methods = umq::PMethodList::make();
    {
        auto m = methods.lock();
        m->proxies.emplace("echo.",[&](umq::Request &&req) {
           umq::PPeer peer = req.lock_peer();
           auto name = req.get_method_name();
           auto subname = name.substr(5);
           auto data = req.get_data();
           peer->call(subname, data, [req = std::move(req)](umq::Response &&resp) mutable {
                if (resp.is_result()) {
                    req.send_result(resp.get_data());
                } else if (resp.is_exception()) {
                    req.send_exception(resp.get_data());
                } else if (resp.is_execute_error()) {
                    req.send_execute_error(resp.get_data());
                }
           });
        });
        m->proxies.emplace("callback.",[&](umq::Request &&req) {
           umq::PPeer peer = req.lock_peer();
           auto name = req.get_method_name();
           auto subname = name.substr(9);
           auto data = req.get_data();
           peer->call_callback(subname, data, [req = std::move(req)](umq::Response &&resp) mutable {
                if (resp.is_result()) {
                    req.send_result(resp.get_data());
                } else if (resp.is_exception()) {
                    req.send_exception(resp.get_data());
                } else if (resp.is_execute_error()) {
                    req.send_execute_error(resp.get_data());
                }
           });
        });

        m->methods.emplace("sub_counter", [&](umq::Request &&req) {
           if (req.get_data().empty()) {
               req.send_exception(400, "Topic is not specified defined");
           } else {
               pub_counter.subscribe(req.lock_peer()->start_publish(req.get_data()));
           }
        });

        m->methods.emplace("sub_chat", [&](umq::Request &&req) {
            if (req.get_data().empty()) {
                req.send_exception(400, "Topic is not specified defined");
            } else {
                pub_chat.subscribe(req.lock_peer()->start_publish(req.get_data()));
            }
        });
        m->methods.emplace("send_chat", [&](umq::Request &&req) {
            auto peer = req.lock_peer();
            auto name = peer->get_peer_variable("name");
            if (name.has_value()) {
                std::string msg(*name);
                msg.append(": ").append(req.get_data());
                pub_chat.publish(msg);
            } else{
                req.send_exception(401, "Variable 'name' is not set");
            }
        });

        m->methods.emplace("set_me_var",[&](umq::Request &&req) {
            auto peer = req.get_peer().lock();
            if (peer) {
                auto dt = req.get_data();
                auto var = userver::splitAt("=", dt);
                peer->set_variable(var, dt);
            }
        });

    }


    server.addPath("", StaticWebserver({"tests/web","index.html"}));

    server.addPath("/ws", WebsocketServerHandler([=](WSStream &stream){
        auto peer = umq::Peer::make();
        peer->init_server(std::make_unique<umq::WSConnection>(std::move(stream)), nullptr);
        peer->keep_until_disconnected();
        peer->set_methods(methods);
    }));
    server.start(addrs, createAsyncProvider({1,4}));

    setThreadAsyncProvider(server.getAsyncProvider());
    counter(pub_counter,1);

    server.stopOnSignal();
    server.runAsWorker();
}
