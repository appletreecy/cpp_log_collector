#include "server/UdpServer.h"
#include <iostream>
#include "common/Signal.h"
#include "common/BlockingQueue.h"
#include "writer/LogWriter.h"

int main(){
    try {
        Signal::install();

        BlockingQueue<std::string> queue(10);
        LogWriter writer(queue, "collector.log", 256);
        writer.start();

        UdpServer server(9000, queue);
        server.run();  // blocks until SIGTERM/SIGINT
        writer.stop(); // drain + flush
    } catch(const std::exception& e) {
        std::cerr << "Fatal: " <<e.what() << '\n';
        return 1;
    }
    return 0;
}