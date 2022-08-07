#include "userver/websockets_server_handler.h"
#include "userver/http_server.h"

using namespace userver;

std::string_view page = R"html(<!DOCTYPE html>
<html>
<head>
<title>WebSocket test</title>
</head>
<body>
<div id="area">
</div>
<div>
Type text (Enter): <input type="text" id="text"> 
</div>
<script type="text/javascript">
var loc = window.location, new_uri;
if (loc.protocol === "https:") {
    new_uri = "wss:";
} else {
    new_uri = "ws:";
}
new_uri += "//" + loc.host;
new_uri += loc.pathname + "./ws";
let ws = new WebSocket(new_uri);
ws.onmessage = m => {
    var el = document.createElement("P");
    el.innerText = m.data;
    document.getElementById("area").appendChild(el);
}

document.getElementById("text").addEventListener("keypress",ev=>{
   if (ev.key == "Enter") {
        ws.send(ev.target.value);
        ev.target.value = "";        
    } 
});

</script>

</body>
</html>
)html";



std::vector<WeakWSStreamRef> _publisher;
std::mutex lock;

void publish_text(const std::string_view &text) {
    std::lock_guard _(lock);
    auto iter = std::remove_if(_publisher.begin(), _publisher.end(),
           [&](const WeakWSStreamRef &ref) {
        SharedWSStream s;
        if (ref.lock(s)) {
            return !s.send_text(text);
        }else{
            return true;
        }
    });
    _publisher.erase(iter, _publisher.end());
}

void publish_add(const SharedWSStream &s) {
    std::lock_guard _(lock);
    _publisher.push_back(s);
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
        auto s = stream.make_shared();
        publish_add(s);
        s.recv_loop() >> [s](const WSStream::Message &msg) mutable {
            if (msg.type == WSFrameType::text) {
                publish_text(msg.data);
            }
            return true;
        };
    }));
    server.start(addrs, createAsyncProvider({1,4}));
    server.stopOnSignal();
    server.runAsWorker();
}

