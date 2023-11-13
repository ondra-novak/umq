#include "../umq/connection.h"

class TestOutputConnection: public umq::IConnection {
public:

    struct Output {
        std::ostringstream text_msgs;
        std::ostringstream binary_msgs;
        bool closed = false;
    };

    TestOutputConnection(Output &out):_out(out) {}

    virtual umq::Future<umq::IConnection::Message> receive() override;
    virtual umq::Future<bool> flush() override;
    virtual bool send(const umq::IConnection::Message &msg) override;
    virtual std::size_t getBufferedAmount() const override;

protected:
    Output &_out;
};

inline umq::Future<umq::IConnection::Message> TestOutputConnection::receive() {
    return IConnection::close_message;
}

inline umq::Future<bool> TestOutputConnection::flush() {
    return true;
}

inline bool TestOutputConnection::send(const umq::IConnection::Message &msg) {
    switch (msg.type) {
        case IConnection::Message::text: _out.text_msgs << msg.text; break;
        case IConnection::Message::binary: _out.binary_msgs << msg.text; break;
        case IConnection::Message::close: _out.closed = true;
        default:break;
    }
}

inline std::size_t TestOutputConnection::getBufferedAmount() const {
    return 0;
}
