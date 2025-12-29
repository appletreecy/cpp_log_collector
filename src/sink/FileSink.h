#pragma once
#include <fstream>
#include <string>
#include <string_view>

class FileSink {
public:
    explicit FileSink(const std::string& path);
    ~FileSink();

    // Write one log line(add '\n' if missing)
    void writeLine(std::string_view line);

private:
    std::ofstream out_;
};