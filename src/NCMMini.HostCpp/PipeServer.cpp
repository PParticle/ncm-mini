#include "PipeServer.h"

#include "../NCMMini.Band/Protocol.h"

#include <algorithm>
#include <cstring>

namespace ncmmini
{
namespace
{
bool ReadAll(HANDLE pipe, void* buffer, DWORD size)
{
    auto* bytes = static_cast<std::uint8_t*>(buffer);
    DWORD offset = 0;
    while (offset < size)
    {
        DWORD read = 0;
        if (!ReadFile(pipe, bytes + offset, size - offset, &read, nullptr) || read == 0)
        {
            return false;
        }
        offset += read;
    }
    return true;
}

bool WriteAll(HANDLE pipe, const void* buffer, DWORD size)
{
    const auto* bytes = static_cast<const std::uint8_t*>(buffer);
    DWORD offset = 0;
    while (offset < size)
    {
        DWORD written = 0;
        if (!WriteFile(pipe, bytes + offset, size - offset, &written, nullptr) || written == 0)
        {
            return false;
        }
        offset += written;
    }
    return true;
}
}

PipeServer::PipeServer(CommandHandler handler) : handler_(std::move(handler))
{
}

PipeServer::~PipeServer()
{
    Stop();
}

void PipeServer::Start()
{
    stopping_ = false;
    worker_ = std::thread(&PipeServer::Run, this);
}

void PipeServer::Stop()
{
    if (!worker_.joinable())
    {
        return;
    }
    stopping_ = true;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    {
        std::lock_guard lock(connectionMutex_);
        pipe = connection_;
    }
    if (pipe != INVALID_HANDLE_VALUE)
    {
        DisconnectNamedPipe(pipe);
    }
    CancelSynchronousIo(reinterpret_cast<HANDLE>(worker_.native_handle()));
    HANDLE wake = CreateFileW(ncmmini::PipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (wake != INVALID_HANDLE_VALUE)
    {
        CloseHandle(wake);
    }
    worker_.join();
}

void PipeServer::Publish(const BandState& state)
{
    {
        std::lock_guard lock(stateMutex_);
        state_ = state;
    }
    std::lock_guard connectionLock(connectionMutex_);
    if (connection_ == INVALID_HANDLE_VALUE)
    {
        return;
    }
    WriteState(connection_, state);
}

void PipeServer::Run()
{
    while (!stopping_)
    {
        HANDLE pipe = CreateNamedPipeW(
            ncmmini::PipeName,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            64 * 1024,
            64 * 1024,
            1000,
            nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            Log(L"CreateNamedPipe failed: " + std::to_wstring(GetLastError()));
            return;
        }
        const BOOL connected = ConnectNamedPipe(pipe, nullptr)
            ? TRUE
            : (GetLastError() == ERROR_PIPE_CONNECTED ? TRUE : FALSE);
        if (!connected)
        {
            CloseHandle(pipe);
            if (!stopping_) std::this_thread::yield();
            continue;
        }
        {
            std::lock_guard lock(connectionMutex_);
            connection_ = pipe;
        }
        BandState current;
        {
            std::lock_guard lock(stateMutex_);
            current = state_;
        }
        WriteState(pipe, current);

        ncmmini::CommandPacket packet{};
        while (!stopping_ && ReadAll(pipe, &packet, sizeof(packet)))
        {
            if (packet.magic != ncmmini::ProtocolMagic || packet.version != ncmmini::ProtocolVersion
                || packet.message != ncmmini::CommandMessage)
            {
                Log(L"invalid DeskBand command packet");
                break;
            }
            const auto command = static_cast<BandCommand>(packet.command);
            if (command == BandCommand::Previous || command == BandCommand::PlayPause
                || command == BandCommand::Next || command == BandCommand::Exit)
            {
                handler_(command);
            }
        }
        {
            std::lock_guard lock(connectionMutex_);
            if (connection_ == pipe) connection_ = INVALID_HANDLE_VALUE;
        }
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

void PipeServer::WriteState(HANDLE pipe, const BandState& state)
{
    if (pipe == INVALID_HANDLE_VALUE)
    {
        return;
    }
    const auto title = WideToUtf8(state.title);
    const auto artist = WideToUtf8(state.artist);
    const auto lyric = WideToUtf8(state.lyric);
    const auto coverSize = state.cover.size() == 40 * 40 * 4 ? state.cover.size() : 0;
    ncmmini::StateHeader header{
        ncmmini::ProtocolMagic,
        ncmmini::ProtocolVersion,
        ncmmini::StateMessage,
        state.running ? 1u : 0u,
        static_cast<std::uint32_t>(title.size()),
        static_cast<std::uint32_t>(artist.size()),
        static_cast<std::uint32_t>(lyric.size()),
        static_cast<std::uint32_t>(coverSize)
    };
    std::lock_guard lock(writeMutex_);
    if (!WriteAll(pipe, &header, sizeof(header))) return;
    if (!title.empty() && !WriteAll(pipe, title.data(), static_cast<DWORD>(title.size()))) return;
    if (!artist.empty() && !WriteAll(pipe, artist.data(), static_cast<DWORD>(artist.size()))) return;
    if (!lyric.empty() && !WriteAll(pipe, lyric.data(), static_cast<DWORD>(lyric.size()))) return;
    if (coverSize != 0) WriteAll(pipe, state.cover.data(), static_cast<DWORD>(coverSize));
}
}
