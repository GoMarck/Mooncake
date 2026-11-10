#include "real_client.h"

#include <chrono>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
volatile std::sig_atomic_t stopped = 0;

void Stop(int) { stopped = 1; }

size_t ParseSize(const char* text) {
    const std::string value(text);
    size_t end = 0;
    const auto bytes = std::stoull(value, &end);
    if (value.empty() || value.front() == '-' || end != value.size() ||
        bytes == 0) {
        throw std::invalid_argument("sizes must be positive byte counts");
    }
    return bytes;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 6) {
        std::cerr << "Usage: " << argv[0]
                  << " HOST MASTER SEGMENT_BYTES BUFFER_BYTES PROTOCOL\n";
        return 2;
    }
    std::signal(SIGINT, Stop);
    std::signal(SIGTERM, Stop);
    try {
        auto client = mooncake::RealClient::create();
        if (!client) return 1;
        if (client->setup_real(argv[1], "P2PHANDSHAKE", ParseSize(argv[3]),
                               ParseSize(argv[4]), argv[5], "", argv[2]) != 0) {
            return 1;
        }
        std::cout << "STORE_READY\n" << std::flush;
        while (!stopped) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return client->tearDownAll() == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
