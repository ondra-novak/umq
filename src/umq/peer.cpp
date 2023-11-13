#include "peer.h"

#include <sstream>
#include <queue>
#include <mutex>
#include <optional>
#include <functional>


namespace umq {

Peer::~Peer() {

}

class InvalidIDFormat: public std::exception {
public:
    const char *what() const noexcept override {return "Invalid format of message ID";}
};

struct Peer::Sender {
    std::shared_ptr<Core> _owner_locked;
    std::queue<SharedFuture<BinaryPayload> > _attachments;
    Future<bool> _flusher;
    SharedFuture<BinaryPayload> _cur_waiting;
    bool _force_stop = false;
    union {
        Future<bool>::Target _flusher_target;
        Future<BinaryPayload>::Target _cur_waiting_target;
    };

    bool is_active() const {return static_cast<bool>(_owner_locked) ;};
    void start(std::shared_ptr<Core> owner);
    void cycle();

    void on_flush(Future<bool> *f) noexcept;
    void on_attachment_ready(Future<BinaryPayload> *f) noexcept;
};


struct Peer::Receiver {
    std::shared_ptr<Core> _owner_locked;
    Future<IConnection::Message> _reader;
    Future<IConnection::Message>::Target _reader_target;

    void start(std::shared_ptr<Core> owner);
    void on_data(Future<IConnection::Message> *f) noexcept;
};

unsigned int Peer::Error::get_code() const {
    return std::strtoul(msg.c_str(), nullptr, 10);
}
std::string_view Peer::Error::get_message() const {
    char *x;
    std::strtoul(msg.c_str(), &x, 10);
    if (*x != ' ') return msg;
    else return std::string_view(x+1);
}

struct Peer::UnsubscribeNotify {
    std::function<void()> _fn;
    UnsubscribeNotify() = default;
    UnsubscribeNotify(std::function<void()> fn):_fn(std::move(fn)) {}
    ~UnsubscribeNotify() {
        if (_fn) _fn();
    }
};

struct Peer::Core: std::enable_shared_from_this<Peer::Core> {
    std::unique_ptr<IConnection> _conn;
    std::mutex _mx;

    Future<void>::Promise _end_monitor;
    Future<Payload>::Promise _welcome;
    Future<Payload>::Promise _hello;
    Future<Payload>::Promise _rpc;
    Future<Payload>::LazyTarget _rpc_lazy;

    std::recursive_mutex _send_mutex;
    std::ostringstream _send_buff;
    Sender _attach_sender;
    Receiver _receiver;


    std::queue<Promise<BinaryPayload> > _waiting_attachments;
    ID _id_gen = 1;

    std::unordered_map<ID, Future<Payload>::Promise> _pending_rpc;
    std::unordered_map<ID, Future<CallbackCall>::Promise> _pending_callbacks;
    std::unordered_map<ID, Future<Payload>::Promise> _subscriptions;
    std::unordered_map<ID, UnsubscribeNotify> _topics;
    std::unordered_map<std::string, Payload> _attributes;


    Core(std::unique_ptr<IConnection> &&con)
        :_conn(std::move(con))
        ,_rpc_lazy( Future<Payload>::LazyTarget::member_fn<&Core::on_begin_rpc>(this))
    {
    }

    ~Core() {
        _conn = nullptr;
    }

    void start() {
        _receiver.start(shared_from_this());
    }
    bool process_message(const IConnection::Message &msg);
    void process_text_message(const std::string_view &data, Attachments && att={});
    void process_binary_message(const std::string_view &data);
    void on_stream_close();
    bool send(char cmd, ID id, const std::string_view &message, Attachments &&attachs);
    template<typename Fn>
    bool send_fn(char cmd, ID id, Fn fn, Attachments &&attachs);
    bool send_fatal_error(unsigned int code, const std::string_view &message);
    bool send_fatal_error(unsigned int code);
    void on_receive(Future<IConnection::Message> *f) noexcept;
    template<typename T> auto pick_promise(T &map, ID id);
    void process_callback_call(ID id, const std::string_view &payload, Attachments &&att);
    void process_topic_update(ID id, const std::string_view &payload, Attachments &&att);
    void process_attribute_set(const std::string_view &payload, Attachments &&att);
    void process_attribute_reset(const std::string_view &payload);
    void on_begin_rpc(Future<Payload>::Promise payload) noexcept;
    void on_unsubscribe(ID id) noexcept;

};

void Peer::Receiver::start(std::shared_ptr<Core> owner) {
    _owner_locked = std::move(owner);
    _reader_target = _reader_target.template member_fn<&Receiver::on_data>(this);
    _reader << [&]{return _owner_locked->_conn->receive();};
}

void Peer::Receiver::on_data(Future<IConnection::Message> *f) noexcept {
    try {
        IConnection::Message data = *f;
        if (_owner_locked->process_message(data)) {
            _reader << [&]{return _owner_locked->_conn->receive();};
        }
        auto own = std::move(_owner_locked);
        own->on_stream_close();
    } catch (...) {
        auto own = std::move(_owner_locked);
        own->on_stream_close();
    }
}


struct Peer::PendingCallback {
    std::weak_ptr<Core> _core;
    ID _id;
    Future<CallbackCall::Result> _result_wait;
    Future<CallbackCall::Result>::Target _result_wait_target;
    void on_result(Future<CallbackCall::Result> *f) noexcept {
        std::shared_ptr<Core> lk_core =_core.lock();
        if (lk_core) {
            try {
                CallbackCall::Result &p = *f;
                lk_core->send(cmd_rpc_result, _id, p._text, std::move(p._attachments));
            } catch (const std::exception &e) {
                lk_core->send(cmd_rpc_exception, _id, e.what(), {});
            }
        }
        delete this;
    }
    PendingCallback(std::weak_ptr<Peer::Core> core, ID id)
        :_core(std::move(core))
        ,_id(id)
        ,_result_wait_target(Future<CallbackCall::Result>::Target::member_fn<&PendingCallback::on_result>(this))
    {}
    Future<CallbackCall::Result>::Promise charge() {
        auto p = _result_wait.get_promise();
        _result_wait.register_target(_result_wait_target);
        return p;
    }
};

void Peer::Core::process_binary_message(const std::string_view &data) {
    if (_waiting_attachments.empty()) return;
    Promise<BinaryPayload> p = std::move(_waiting_attachments.front());
    _waiting_attachments.pop();
    p(data.begin(), data.end());
}

template<typename T>
auto Peer::Core::pick_promise(T &map, ID id) {
    std::lock_guard _(_mx);
    auto iter = map.find(id);
    if (iter == map.end()) return decltype(iter->second)();
    auto p = std::move(iter->second);
    map.erase(iter);
    return p;
}

void Peer::Core::on_unsubscribe(ID id) noexcept {
    UnsubscribeNotify ntf;
    {
        std::lock_guard _(_mx);
        auto iter = _topics.find(id);
        if (iter == _topics.end()) return;
        ntf = std::move(iter->second);
        _topics.erase(iter);
    }

}

bool Peer::Core::process_message(const IConnection::Message &msg) {
    switch (msg.type) {
        default:
        case IConnection::Message::close:
            return false;
        case IConnection::Message::text:
            process_text_message(msg.data);
            return true;
        case IConnection::Message::binary:
            process_binary_message(msg.data);
            return true;
    }
}

void Peer::Core::on_stream_close() {
    //clear any pending promise
    decltype(_waiting_attachments) waiting_attachments;
    decltype(_pending_rpc) pending_rpc;
    decltype(_pending_callbacks) pending_callbacks;
    decltype(_subscriptions) subscriptions;
    decltype(_topics) topics;
    {
        waiting_attachments = std::move(_waiting_attachments);
        pending_rpc = std::move(_pending_rpc);
        pending_callbacks = std::move(_pending_callbacks);
        subscriptions = std::move(_subscriptions);
        topics = std::move(_topics);
        //force stop sending attachments
        _attach_sender._force_stop = true;
    }
    _hello.drop();
    _welcome.drop();
    _rpc.drop();
    //if exception
    auto e = std::current_exception();
    //reject witj exception
    if (e) _end_monitor.reject(e);
    //on fulfill end monitoring promise
    else _end_monitor();
}

void Peer::Core::process_text_message(const std::string_view &data, Attachments && att) {
    auto sep = data.find(':');
    if (sep == data.npos) {
        send_fatal_error(err_protocol_error);
        return;
    }
    auto cmd_part = data.substr(0, sep);
    auto payload_part = data.substr(sep+1);
    if (cmd_part.empty()) {
        send_fatal_error(err_protocol_error);
        return;
    }
    char cmd = cmd_part[0];
    ID id = fromBase36(cmd_part.substr(1));
    if (cmd == cmd_attachment) {
        for (ID i = 0; i < id; ++i) {
            att.push_back([&](auto promise){
                _waiting_attachments.push(std::move(promise));
            });
        }
        process_text_message(payload_part, std::move(att));
        return;
    }

    switch (cmd) {
        default:
            send_fatal_error(err_unsupported_command);
            return;
        case cmd_attachment_error: if (!_waiting_attachments.empty()) {
                    auto p = std::move(_waiting_attachments.front());
                    _waiting_attachments.pop();
                    p.reject(Error(payload_part));
                };break;
        case cmd_fatal_error:
            _hello.reject(Error(payload_part));
            _welcome.reject(Error(payload_part));
            _end_monitor.reject(Error(payload_part));
            break;
        case cmd_hello:
            if (id < version) {
                send_fatal_error(err_unsupported_version);
            } else {
                _hello(id, std::string(payload_part), std::move(att));
            }
            break;
        case cmd_welcome:
            if (id < version) {
                send_fatal_error(err_unsupported_version);
            } else {
                _welcome(id, std::string(payload_part), std::move(att));
            }
            break;
        case cmd_rpc_call:
            if (!_rpc(id, std::string(payload_part), std::move(att))) {
                send_fn(cmd_rpc_error, id, format_error(err_no_rpc),{});
            }
            break;
        case cmd_rpc_result:
            pick_promise(_pending_rpc, id)(id, std::string(payload_part), std::move(att));
            break;
        case cmd_rpc_exception:
            pick_promise(_pending_rpc, id).reject(Error(payload_part));
            break;
        case cmd_callback_call:
            process_callback_call(id, payload_part, std::move(att));
            break;
        case cmd_topic_update:
            process_topic_update(id, payload_part, std::move(att));
            break;
        case cmd_topic_close:
            pick_promise(_subscriptions, id).reject(SubscriptionClosed());
            break;
        case cmd_topic_unsubscribe:
            on_unsubscribe(id);
            break;
        case cmd_attribute_set:
            process_attribute_set(payload_part, std::move(att));
            break;
        case cmd_attribute_reset:
            process_attribute_reset(payload_part);
            break;

    }



}


void Peer::Sender::start(std::shared_ptr<Peer::Core> owner) {
    _owner_locked = std::move(owner);
    cycle();
}
void Peer::Sender::cycle() {
    if (_attachments.empty()) {
        _owner_locked.reset();
        return;
    }
    _cur_waiting = std::move(_attachments.front());
    _attachments.pop();
    _cur_waiting_target = _cur_waiting_target.template member_fn<&Sender::on_attachment_ready>(this);
    _cur_waiting.register_target(_cur_waiting_target);
}

void Peer::Sender::on_attachment_ready(Future<BinaryPayload> *f) noexcept {
    try {
        const BinaryPayload &pl = *f;
        if (_force_stop || !_owner_locked->_conn->send({
            std::string_view(reinterpret_cast<const char *>(pl.data()), pl.size()),
            IConnection::Message::binary
        })) {
            std::lock_guard _(_owner_locked->_send_mutex);
            _attachments = {};
            cycle();
        }
        _flusher << [&]{return _owner_locked->_conn->flush();};
        _flusher_target = _flusher_target.member_fn<&Sender::on_flush>(this);
        _flusher.register_target(_flusher_target);
    } catch (const std::exception &e) {
        _owner_locked->send(cmd_attachment_error, 0, e.what(), {});
    }
}

void Peer::Sender::on_flush(Future<bool> *f) noexcept {
    std::lock_guard _(_owner_locked->_send_mutex);
    try {
        bool res = *f;
        if (res) {
            cycle();
            return;
        }
    } catch (...) {
        //empty
    }
    _attachments = {};
    cycle();
}



template<typename Fn>
bool Peer::Core::send_fn(char cmd, ID id, Fn fn, Attachments &&attachments) {
    std::lock_guard _(_send_mutex);
    _send_buff.str(std::string());
    bool has_attach = !attachments.empty();
    if (has_attach) {
        _send_buff << cmd_attachment;
        toBase36(attachments.size(), _send_buff);
        _send_buff << ':';
    }
    _send_buff << cmd;
    toBase36(id, _send_buff);
    _send_buff << ':';
    fn(_send_buff);
    if (!_conn->send({_send_buff.view(), IConnection::Message::text})) {
        _attach_sender._attachments = {};
        return false;
    }

    if (has_attach) {
        for (auto &x: attachments) {
            _attach_sender._attachments.push(std::move(x));
        }
        if (!_attach_sender.is_active()) {
            _attach_sender.start(shared_from_this());
        }
    }
    return true;
}
bool Peer::Core::send(char cmd, ID id, const std::string_view &message, Attachments &&attachments) {
    return send_fn(cmd, id, [&](std::ostream &s) {s << message;}, std::move(attachments));
}

bool Peer::Core::send_fatal_error(unsigned int code, const std::string_view &message) {
    return send_fn(cmd_fatal_error, 0, [&](std::ostream &s){
        s << code << " " << message;
    },{});
    return _conn->send(IConnection::close_message);
}

bool Peer::Core::send_fatal_error(unsigned int code) {
    return send_fatal_error(code, errorMessage(code));
}

Future<Peer::Payload> Peer::start_client(std::unique_ptr<IConnection> conn,
        const std::string_view &message, Attachments &&attachments) {

    _core = std::make_shared<Core>(std::move(conn));
    return [&](auto promise){
        _core->_welcome = std::move(promise);
        _core->start();
        _core->send(cmd_hello, version, message, std::move(attachments));
    };
}

Future<Peer::Payload> Peer::start_server(std::unique_ptr<IConnection> conn) {
    _core = std::make_shared<Core>(std::move(conn));
    return [&](auto promise){
        _core->_hello = std::move(promise);
        _core->start();
    };

}

void Peer::accept_client(const std::string_view &message, Attachments &&attachments) {
    _core->send(cmd_welcome, version, message, std::move(attachments));
}

void Peer::reject_client(const std::string_view &message) {
    _core->send_fatal_error(err_rejected, message);
}

Future<void> Peer::close_event() {
    return [&](auto promise) {
        _core->_end_monitor = std::move(promise);
    };
}
Future<Peer::Payload> Peer::rpc_call(const std::string_view &message, Attachments &&attachments) {
    return [&](auto promise) {
        Core &c = *_core;
        std::lock_guard _(c._mx);
        ID id = c._id_gen++;
        c._pending_rpc.emplace(id, std::move(promise));
        c.send(cmd_rpc_call, id, message, std::move(attachments));
    };
}
Future<Peer::Payload> Peer::rpc_server() {
    return _core->_rpc_lazy; //set lazy target
}

void Peer::Core::on_begin_rpc(Future<Payload>::Promise payload) noexcept {
    //lazy operation
    _rpc = std::move(payload);
}

void Peer::rpc_result(ID id, const std::string_view &response, Attachments &&attachments) {
    _core->send(cmd_rpc_result, id, response, std::move(attachments));
}

void Peer::rpc_exception(ID id, const std::string_view &message) {
    _core->send(cmd_rpc_exception, id, message, {});
}

Peer::ID Peer::create_subscription() {
    Core &c = *_core;
    std::lock_guard _(c._mx);
    return c._id_gen++;
}

bool Peer::Subscription::check() const {
    auto core = _target.lock();
    if (!core) return false;
    Core &c = *core;
    std::lock_guard _(c._mx);
    return c._topics.find(_id) != c._topics.end();
}

bool Peer::Subscription::publish(const std::string_view &data, umq::Peer::Attachments &&attachments) {
    auto core = _target.lock();
    if (!core) return false;
    Core &c = *core;
    {
        std::lock_guard _(c._mx);
        if (c._topics.find(_id) == c._topics.end()) return false;
    }
    c.send(cmd_topic_update,_id, data, std::move(attachments));
    return true;
}

void Peer::Subscription::close() {
    auto core = _target.lock();
    if (!core) return;
    Core &c = *core;
    {
        std::lock_guard _(c._mx);
        if (c._topics.find(_id) == c._topics.end()) return;
    }
    c.send(cmd_topic_close,_id, {}, {});
}

Peer::ID Peer::Subscription::get_id() const {
    return _id;
}

std::optional<Peer> Peer::Subscription::get_peer() const {
    auto core = _target.lock();
    if (!core) return {};
    return Peer(std::move(core));
}

Peer::Subscription::Subscription(std::shared_ptr<Peer::Core> target, ID id)
    :_target(target),_id(id) {}

bool Peer::Subscription::on_unsubscribe(std::function<void()> fn) {
    auto core = _target.lock();
    if (!core) return false;
    Core &c = *core;
    std::lock_guard _(c._mx);
    auto iter = c._topics.find(_id);
    if (iter == c._topics.end()) return false;
    iter->second = std::move(fn);
    return true;
}

Future<Peer::Payload> Peer::listen_subscription(ID subscription) {
    Core &c = *_core;
    std::lock_guard _(c._mx);
    return [&](auto promise){
        c._subscriptions[subscription] = std::move(promise);
    };
}

Peer::Subscription Peer::begin_publish(ID subscription) {

    Core &c = *_core;
    std::lock_guard _(c._mx);
    c._topics.emplace(subscription, UnsubscribeNotify());
    return Subscription(_core, subscription);
}

Peer::Callback Peer::create_callback_call() {
    Core &c = *_core;
    std::lock_guard _(c._mx);
    ID id = c._id_gen++;
    return {
        id,
        [&](auto promise) {
            c._pending_callbacks.emplace(id, std::move(promise));
        }
    };
}

void Peer::cancel_callback_call(ID callback_id) {
    Core &c = *_core;
    std::lock_guard _(c._mx);
    c._pending_callbacks.erase(callback_id);
}

Future<Peer::Payload> Peer::rpc_callback_call(ID cb_id, const std::string_view &message, Attachments &&attachments) {
    return [&](auto promise) {
        Core &c = *_core;
        std::lock_guard _(c._mx);
        ID id = c._id_gen++;
        c._pending_rpc.emplace(id, std::move(promise));
        c.send_fn(cmd_rpc_call, id, [&](auto &s) {
            toBase36(cb_id, s);
            s << ':' << message;
        }, std::move(attachments));
    };
}



void Peer::Core::process_callback_call(ID id, const std::string_view &payload, Attachments &&att) {
    auto pos = payload.find(':');
    if (pos == payload.npos) {
        send_fatal_error(err_protocol_error);
        return;
    }
    auto p = payload.substr(pos+1);
    auto cb_id_str = payload.substr(0, pos);
    ID cb_id = fromBase36(cb_id_str);
    auto cbp = pick_promise(_pending_callbacks, cb_id);
    if (cbp) {
        Future<CallbackCall::Result>::Promise respond;
        auto pc = new PendingCallback(weak_from_this(), id);
        cbp(id, std::string(p), std::move(att), pc->charge());
    } else {
        send_fatal_error(err_callback_not_found);
    }
}

void Peer::Core::process_topic_update(ID id, const std::string_view &payload, Attachments &&att) {
    Future<Payload>::Promise *subs = nullptr;
    {
        std::lock_guard _(_mx);
        auto iter = _subscriptions.find(id);
        if (iter != _subscriptions.end()) {
            subs = &iter->second;
        }
    }
    if (subs) {
        (*subs)(id, payload, std::move(att));
        if (*subs) return;
        std::lock_guard _(_mx);
        _subscriptions.erase(id);
    }
    send(cmd_topic_unsubscribe, id, {}, {});
}

Future<bool> Peer::flush() {
    return _core->_conn->flush();
}

std::string_view Peer::errorMessage(unsigned int error) {
    switch (error) {
        case err_callback_not_found: return "Callback not found";
        case err_protocol_error: return "Protocol format error";
        case err_rejected: return "Client rejected";
        case err_unsupported_command: return "Unsupported command";
        case err_unsupported_version: return "Unsupported version";
        case err_no_rpc: return "Not RPC server";
        case err_rpc_route_error : return "No route to requested method";
        case err_rpc_temporary_unavailable: return "Temporarily unavailable";
        default: return "Unknown error code";
    }
}


void Peer::close() {
    //send close
    _core->_conn->send(IConnection::close_message);
    //shutdown attach sender
    _core->_attach_sender._force_stop = true;
}
void Peer::shutdown() {
    //shutdown the receiver
    _core->_conn->shutdown();
}

void Peer::toBase36(ID id, std::ostream &out) {
    char buff[50];
    char *c = buff;
    while (id) {
        auto p = id % 36;
        id = id / 36;
        if (p < 10) *c++ = '0' + p;
        else *c++ = 'A' + (p - 10);
    }
    while (c != buff) {
        --c;
        out.put(*c);
    }
}

Peer::ID Peer::fromBase36(std::string_view txt) {
    ID accum = 0;
    for (char c: txt) {
        if (c>='0' && c<='9') accum = accum * 36 + (c - '0');
        else if (c >= 'A' && c<='Z') accum = accum * 36 + (c - 'A' + 10);
        else throw InvalidIDFormat();
    }
    return accum;
}

void Peer::set_attribute(const std::string_view &attribute_name,
        const std::string_view &attribute_value, Attachments &&attachments) {
    _core->send_fn(cmd_attribute_set, 0, [&](auto &s){
        s << attribute_name << '=' << attribute_value;
    }, std::move(attachments));
}

void Peer::clear_attribute(const std::string_view &attribute_name) {
    _core->send(cmd_attribute_reset, 0, attribute_name, {});
}

void Peer::Core::process_attribute_set(const std::string_view &payload, Attachments &&att) {
    auto pos = payload.find('=');
    if (pos == payload.npos) {
        send_fatal_error(err_protocol_error);
        return;
    }
    std::string attr_name ( payload.substr(0,pos));
    std::string attr_value ( payload.substr(pos+1));
    std::lock_guard _(_mx);
    Payload &p = _attributes[attr_name];
    p.text = std::move(attr_value);
    p.attachments = std::move(att);
}

void Peer::Core::process_attribute_reset(const std::string_view &payload) {
    std::lock_guard _(_mx);
    _attributes.erase(std::string(payload));
}


}
