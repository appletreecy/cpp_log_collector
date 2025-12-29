#include "server/UdpServer.h"
#include <iostream>
#include "common/Signal.h"

int main(){
    try {
        Signal::install();

        UdpServer server(9000, "collector.log");
        server.run();
    } catch(const std::exception& e) {
        std::cerr << "Fatal: " <<e.what() << '\n';
        return 1;
    }
    return 0;
}