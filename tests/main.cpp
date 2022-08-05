#include <userver/async_provider.h>
#include <userver/connect.h>
#include <userver/netaddr.h>
#include <userver/socket.h>
#include <userver/stream.h>
#include "userver/sha1.h"
#include <iomanip>
#include <iostream>
#include <thread>

#include "../publisher.h"


void read_cycle(userver::Stream &s) {
    s.read() >> [&](const std::string_view &c) {
        if (c.empty()) {
            if (s.timeouted()) {
                s.clear_timeout();
                read_cycle(s);
            }
        } else {
            std::cout << c << std::endl;
            read_cycle(s);
        }
    };
}

int main(int argv, char **argc) {
    auto addr = userver::NetAddr::fromString("localhost", "10000");
    auto provider = userver::createAsyncProvider({1,6,false,true});
    userver::setCurrentAsyncProvider(provider);

    bool exit_cycle = false;


    connect(addr) >> [&](std::optional<userver::Stream> &&stream) {

        if (stream.has_value()) {
           userver::Stream s(std::move(*stream));
           std::string t;
           std::size_t n = 0;

           read_cycle(s);

           while (!exit_cycle) {
                t = std::to_string(n);
                t.append("\n");
                s.write(t, true) >> [&](bool x){
                    if (x == false)
                        exit_cycle = true;
                };
                n++;
           }
           std::cout << "Cycle exit" << std::endl;
        } else {
            std::cout << "Connect error" << std::endl;
        }

    };

    std::cout << "Press enter (1)" << std::endl;
    std::cin.get();
    exit_cycle = true;
    std::cout << "Press enter (2)" << std::endl;
    std::cin.get();
    provider.stop();
}
