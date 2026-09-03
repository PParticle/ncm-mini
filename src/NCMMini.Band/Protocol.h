#pragma once

#include <windows.h>

#include <cstdint>

namespace ncmmini
{
constexpr std::uint32_t ProtocolMagic = 0x314D434E;
constexpr std::uint16_t ProtocolVersion = 1;
constexpr std::uint16_t StateMessage = 1;
constexpr std::uint16_t CommandMessage = 2;
constexpr wchar_t PipeName[] = L"\\\\.\\pipe\\NCMMini-v1";

enum class Command : std::uint32_t
{
    Previous = 1,
    PlayPause = 2,
    Next = 3,
    Exit = 4,
    Options = 5
};

#pragma pack(push, 1)
struct StateHeader
{
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t message;
    std::uint32_t flags;
    std::uint32_t titleBytes;
    std::uint32_t artistBytes;
    std::uint32_t lyricBytes;
    std::uint32_t coverBytes;
};

struct CommandPacket
{
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t message;
    std::uint32_t command;
};
#pragma pack(pop)
}
