#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

class RotatingFileSink {
public:
    RotatingFileSink(std::string basePath, std::uint64_t maxBytes, int maxFiles);

    void writeLine(std::string_view line);

private:
    void openIfNeeded_();
    void rotateIfNeeded_(std::uint64_t incomingBytes);
    void rotate_();
    void reopen_();
    std::uint64_t currentSize_() const;

private:
    std::string basePath_;
    std::int64_t maxBytes_;
    int maxFiles_;

    std::ofstream out_;
    std::uint64_t bytesWritten_{0}; // tracks size on disk + this process wirtes
};