#include "../umq/peer.h"
#include "test_connection.h";


int main() {

    using namespace umq;

    TestOutputConnection::Output out;
    Peer peer;
    auto f = peer.start_client(std::make_unique<TestOutputConnection>(out), "Hello world", {});
    peer.close();


}
