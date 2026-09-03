#pragma once

#include "Host.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace ncmmini
{
struct LyricLine
{
    std::chrono::milliseconds position{};
    std::wstring text;
};

class TrackCatalog
{
public:
    TrackInfo Find(const std::wstring& title);

private:
    void EnsureLoaded();

    std::filesystem::path dataDirectory_ = std::filesystem::path(LocalAppDataPath()) / L"NetEase" / L"CloudMusic" / L"webdata" / L"file";
    std::chrono::steady_clock::time_point lastLoad_{};
    std::vector<TrackInfo> tracks_;
};

class LyricsStore
{
public:
    std::vector<LyricLine> Find(const TrackInfo& track) const;
    static std::vector<LyricLine> Parse(const std::string& text);
    static std::wstring Current(const std::vector<LyricLine>& lines, std::chrono::milliseconds elapsed);

private:
    std::filesystem::path dataDirectory_ = std::filesystem::path(LocalAppDataPath()) / L"NetEase" / L"CloudMusic" / L"webdata" / L"file";
};

std::vector<std::uint8_t> LoadCover(const std::wstring& url);
}
