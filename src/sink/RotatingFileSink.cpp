#include "sink/RotatingFileSink.h"

#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

RotatingFileSink::RotatingFileSink(std::string basePath, std::uint64_t maxBytes, int maxFiles)
        : basePath_(std::move(basePath)),
          maxBytes_(maxBytes),
          maxFiles_(maxFiles) {
            if (maxBytes_ == 0) throw std::runtime_error("maxBytes must be > 0");
            if (maxFiles_ < 1) throw std::runtime_error("maxFiles must be >= 1");
            openIfNeeded_();
        }

std::uint64_t RotatingFileSink::currentSize_() const {
    std::error_code ec;
    auto p = fs::path(basePath_);
    if (!fs::exists(p, ec)) return 0;
    auto sz = fs::file_size(p, ec);
    return ec ? 0 : static_cast<std::uint64_t>(sz);
}

void RotatingFileSink::openIfNeeded_() {
    if (out_.is_open()) return;

    out_.open(basePath_, std::ios::app);
    if (!out_.is_open()) {
        throw std::runtime_error("Can't open log file " + basePath_);
    }

    // track existing size if file already exists
    bytesWritten_ = currentSize_();

    // For now: saftest behavior (flush each insertion). Later we can tune.
    out_.setf(std::ios::unitbuf);
}

void RotatingFileSink::reopen_() {
    if (out_.is_open()) out_.close();

    out_.open(basePath_, std::ios::app);
    if (!out_.is_open()) {
        throw std::runtime_error("Can't reopen log file " + basePath_);
    }

    bytesWritten_ = currentSize_();
    out_.setf(std::ios::unitbuf);
}

void RotatingFileSink::rotate_() {
    std::error_code ec;

    // Delete the oldest: base.log.<maxFiles>
    fs::path oldest = basePath_ + "." + std::to_string(maxFiles_);
    if (fs::exists(oldest, ec)) {
        fs::remove(oldest, ec);
    }

    // Shift .(n-1) -> .n
    for (int i =maxFiles_ - 1; i >= 1; --i) {
        fs::path from = basePath_ + "." + std::to_string(i);
        fs::path to = basePath_ + "." + std::to_string(i+1);

        if (fs::exists(from, ec)) {
            fs::rename(from, to, ec);
            if (ec) {
                // if rename fails, ignore and continue (best-effort rotation)
                ec.clear();
            }
        }
    }

    // base -> .1
    fs::path base = basePath_;
    fs::path to1 = basePath_ + ".1";
    if (fs::exists(base, ec)) {
        fs::rename(base, to1, ec);
        if (ec) ec.clear();
    }
}

void RotatingFileSink::rotateIfNeeded_(std::uint64_t incomingBytes) {
    if (bytesWritten_ + incomingBytes < maxBytes_) return;

    // Ensure file is closed before rename
    if (out_.is_open()) out_.flush();
    if (out_.is_open()) out_.close();

    rotate_();
    reopen_();
}

void RotatingFileSink::writeLine(std::string_view line) {
    openIfNeeded_();

    // bytes we will write (line + optional '\n')
    std::uint64_t add = static_cast<std::uint64_t>(line.size());
    if (line.empty() || line.back() != '\n') add +=1;

    rotateIfNeeded_(add);

    out_.write(line.data(), static_cast<std::streamsize>(line.size()));
    if (line.empty() || line.back() != '\n') out_.put('\n');

    bytesWritten_ += add;
}