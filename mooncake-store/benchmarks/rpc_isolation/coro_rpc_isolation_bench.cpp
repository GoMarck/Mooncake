// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bench_common.h"
#include <csignal>
#include <ylt/coro_rpc/coro_rpc_client.hpp>
#include <ylt/coro_rpc/coro_rpc_server.hpp>
#include <ylt/coro_io/io_context_pool.hpp>
#include <async_simple/coro/SyncAwait.h>

namespace rpc_isolation {
struct Reply {
    std::string payload;
    uint64_t handlerNs;
};
std::string serverResponse;
volatile std::sig_atomic_t stopRequested = 0;
void RequestStop(int) { stopRequested = 1; }

Reply Echo(std::string_view)
{
    const auto begin = NowNs();
    Reply result{serverResponse, 0};
    result.handlerNs = NowNs() - begin;
    return result;
}

class Client {
public:
    static coro_rpc::coro_rpc_client::config Options(const Config &c)
    {
        coro_rpc::coro_rpc_client::config options;
        options.request_timeout_duration = std::chrono::milliseconds(c.timeoutMs);
        options.connect_timeout_duration = std::chrono::milliseconds(c.timeoutMs);
        return options;
    }
    Client(const Config &c, coro_io::io_context_pool &pool)
        : client_(pool.get_executor(), Options(c)), payload_(c.requestBytes, 'q'), expectedSize_(c.responseBytes)
    {
        const auto error = async_simple::coro::syncAwait(client_.connect(c.host, std::to_string(c.port)));
        if (error) throw std::runtime_error("coro_rpc connection failed");
    }
    CallResult Call()
    {
        const auto begin = NowNs();
        auto reply = async_simple::coro::syncAwait(client_.call<&Echo>(std::string_view(payload_)));
        const auto elapsed = NowNs() - begin;
        const bool valid = reply && reply.value().payload.size() == expectedSize_
                           && (expectedSize_ == 0 || reply.value().payload.front() == 'r');
        return {valid, elapsed, valid ? reply.value().handlerNs : 0};
    }
private:
    coro_rpc::coro_rpc_client client_;
    std::string payload_;
    size_t expectedSize_;
};
}  // namespace rpc_isolation

int main(int argc, char **argv)
{
    using namespace rpc_isolation;
    try {
        const auto c = Parse(argc, argv);
        if (c.mode == "server") {
            serverResponse.assign(c.responseBytes, 'r');
            coro_rpc::coro_rpc_server server(c.serverThreads, c.port, c.host);
            server.register_handler<&Echo>();
            auto started = server.async_start();
            if (started.hasResult()) return 1;
            std::signal(SIGTERM, RequestStop);
            std::signal(SIGINT, RequestStop);
            std::cout << "RPC_ISOLATION_READY" << std::endl;
            while (!stopRequested) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            server.stop();
            return 0;
        }
        auto pool = coro_io::create_io_context_pool(c.ioThreads);
        return Run(c, "coro_rpc", [&](int) { return std::make_unique<Client>(c, *pool); });
    } catch (const std::exception &e) { std::cerr << e.what() << std::endl; return 1; }
}
