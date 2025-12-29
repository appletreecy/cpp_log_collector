#include "server/UdpServer.h"
#include <iostream>

int main(){
    try {
        UdpServer server(9000, "collector.log");
        server.run();
    } catch(const std::exception& e) {
        std::cerr << "Fatal: " <<e.what() << '\n';
        return 1;
    }
    return 0;
}