#pragma once

#include "Host.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace ncmmini
{
class PipeServer
{
public:
    using CommandHandler = std::function<void(BandCommand)>;

    explicit PipeServer(CommandHandler handler);
    ~PipeServer();

    void Start();
    void Stop();
    void Publish(const BandState& state);

private:
    void Run();
    void WriteState(HANDLE pipe, const BandState& state);

    CommandHandler handler_;
    std::atomic_bool stopping_{false};
    std::thread worker_;
    std::mutex stateMutex_;
    std::mutex connectionMutex_;
    std::mutex writeMutex_;
    BandState state_;
    HANDLE connection_ = INVALID_HANDLE_VALUE;
};
}
