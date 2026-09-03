#include "Host.h"
#include "Media.h"
#include "PipeServer.h"

#include <objbase.h>
#include <shellapi.h>

#include <chrono>
#include <mutex>
#include <thread>

namespace ncmmini
{
class Application
{
public:
    explicit Application(AppOptions options) : options_(std::move(options)), player_(options_)
    {
    }

    int Run()
    {
        PipeServer pipe([this](BandCommand command) { HandleCommand(command); });
        pipe.Start();
        RunBandController(L"show");
        if (options_.launchCloudMusic)
        {
            player_.TryLaunch();
        }

        const auto launchDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        bool playerSeen = false;
        std::wstring previousTitle;
        std::wstring publishedTitle;
        std::wstring previousLyric;
        TrackInfo track;
        std::vector<LyricLine> lyrics;
        std::vector<std::uint8_t> cover;
        auto trackStarted = std::chrono::steady_clock::now();
        bool disconnectedPublished = false;

        while (!shutdown_)
        {
            const auto snapshot = player_.ReadSnapshot();
            processId_ = snapshot.processId;
            if (!snapshot.running)
            {
                if (!disconnectedPublished)
                {
                    pipe.Publish({false, L"", L"网易云音乐未连接", L"", {}});
                    disconnectedPublished = true;
                }
                if (playerSeen || (options_.launchCloudMusic && std::chrono::steady_clock::now() >= launchDeadline))
                {
                    break;
                }
                Wait(std::chrono::milliseconds(500));
                continue;
            }

            playerSeen = true;
            disconnectedPublished = false;
            if (snapshot.windowTitle != previousTitle)
            {
                previousTitle = snapshot.windowTitle;
                track = catalog_.Find(snapshot.windowTitle);
                if (track.name.empty()) track = snapshot.track;
                trackStarted = std::chrono::steady_clock::now();
                lyrics = options_.showLyrics ? lyricsStore_.Find(track) : std::vector<LyricLine>{};
                cover = LoadCover(track.coverUrl);
                previousLyric.clear();
                publishedTitle.clear();
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - trackStarted);
            const auto lyric = options_.showLyrics ? LyricsStore::Current(lyrics, elapsed) : std::wstring();
            if (lyric != previousLyric || snapshot.windowTitle != publishedTitle)
            {
                previousLyric = lyric;
                publishedTitle = snapshot.windowTitle;
                pipe.Publish({
                    true,
                    track.name.empty() ? L"网易云音乐" : track.name,
                    track.artist,
                    lyric,
                    cover
                });
            }
            Wait(std::chrono::milliseconds(300));
        }

        if (options_.closeCloudMusicOnExit)
        {
            player_.Close();
        }
        RunBandController(L"hide");
        pipe.Stop();
        return 0;
    }

private:
    void HandleCommand(BandCommand command)
    {
        if (command == BandCommand::Exit)
        {
            shutdown_ = true;
            return;
        }
        const auto processId = processId_.load();
        if (processId != 0 && !player_.Send(command, processId))
        {
            Log(L"CloudMusic command failed: " + std::to_wstring(static_cast<std::uint32_t>(command)));
        }
    }

    void Wait(std::chrono::milliseconds duration) const
    {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        while (!shutdown_ && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }

    AppOptions options_;
    PlayerController player_;
    TrackCatalog catalog_;
    LyricsStore lyricsStore_;
    std::atomic_bool shutdown_{false};
    std::atomic<DWORD> processId_{0};
};
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    int argumentCount = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    const auto options = ncmmini::ParseOptions(argumentCount, arguments);
    if (arguments != nullptr) LocalFree(arguments);

    HANDLE instance = CreateMutexW(nullptr, TRUE, L"Local\\NCMMini.Host");
    if (instance == nullptr)
    {
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (options.launchCloudMusic)
        {
            ncmmini::PlayerController(options).TryLaunch();
        }
        ncmmini::RunBandController(L"show");
        CloseHandle(instance);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 0;
    }

    const int result = ncmmini::Application(options).Run();
    ReleaseMutex(instance);
    CloseHandle(instance);
    if (SUCCEEDED(comResult)) CoUninitialize();
    return result;
}
