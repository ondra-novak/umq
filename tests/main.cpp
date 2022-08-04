#include "userver/sha1.h"
#include <iomanip>
#include <iostream>

int main(int argv, char **argc) {
    std::string d = userver::SHA1::hash("test message test message test message test message test message test message test message test message test message test message test message test message test message test message test message test message test message test message test message test message test message test message test message test message test message test message test message test message test message test message");
    for (char c : d) {
        unsigned char cc = static_cast<unsigned char>(c);
        std::cout << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(cc);
    }
    std::cout << std::endl;
    return 0;
}
