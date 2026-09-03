#pragma once

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace ncmmini
{
struct AppOptions
{
    std::wstring cloudMusicPath;
    bool launchCloudMusic = true;
    bool closeCloudMusicOnExit = true;
    bool showLyrics = true;
};

struct TrackInfo
{
    std::wstring name;
    std::wstring artist;
    std::wstring coverUrl;
    std::wstring trackId;
    std::wstring lyricsId;
};

struct PlayerSnapshot
{
    bool running = false;
    DWORD processId = 0;
    HWND mainWindow = nullptr;
    std::wstring windowTitle;
    TrackInfo track;
};

struct BandState
{
    bool running = false;
    std::wstring title;
    std::wstring artist;
    std::wstring lyric;
    std::vector<std::uint8_t> cover;
};

enum class BandCommand : std::uint32_t
{
    Previous = 1,
    PlayPause = 2,
    Next = 3,
    Exit = 4,
    Options = 5
};

AppOptions ParseOptions(int argumentCount, wchar_t** arguments);
std::wstring Utf8ToWide(const std::string& text);
std::string WideToUtf8(const std::wstring& text);
std::wstring Trim(std::wstring text);
std::pair<std::wstring, std::wstring> ParsePlayerTitle(const std::wstring& title);
std::wstring LocalAppDataPath();
std::wstring ExecutableDirectory();
void Log(const std::wstring& message);
bool RunBandController(const wchar_t* command);

class PlayerController
{
public:
    explicit PlayerController(AppOptions options);

    bool TryLaunch();
    PlayerSnapshot ReadSnapshot() const;
    bool Send(BandCommand command, DWORD processId) const;
    void Close();

private:
    std::wstring ResolvePath() const;

    AppOptions options_;
    DWORD launchedProcessId_ = 0;
};
}
