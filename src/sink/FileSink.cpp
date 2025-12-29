#include "sink/FileSink.h"
#include <stdexcept>

FileSink::FileSink(const std::string& path) : out_(path, std::ios::app){
    if (!out_.is_open()) throw std::runtime_error("Can't open file sink");

    // auto-flush each insertion
    out_.setf(std::ios::unitbuf);
}

FileSink::~FileSink(){
    // ofstream destructor cloese automatically, but explict flush is fine
    if (out_.is_open()) out_.flush();
}

void FileSink::writeLine(std::string_view line){
    if (!out_.is_open()) return;

    out_.write(line.data(), static_cast<std::streamsize>(line.size()));
    if (line.empty() || line.back() != '\n'){
        out_.put('\n');
    }
}