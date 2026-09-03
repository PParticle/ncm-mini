#include "Host.h"
#include "Media.h"
#include "PipeServer.h"
#include "SettingsWindow.h"

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
    explicit Application(AppOptions options)
        : options_(std::move(options)), player_(options_), settingsPath_(SettingsPath()),
          settings_(LoadSettings(settingsPath_)),
          settingsWindow_(settingsPath_, [this](const AppSettings& settings) { UpdateSettings(settings); })
    {
        if (!options_.showLyrics) settings_.showLyrics = false;
        if (!options_.closeCloudMusicOnExit) settings_.closeCloudMusicOnExit = false;
    }

    int Run()
    {
        if (GetFileAttributesW(settingsPath_.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            SaveSettings(settingsPath_, settings_);
        }
        if (!settingsWindow_.Start())
        {
            Log(L"Settings window could not be initialized");
        }
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
        auto coverRetryAt = trackStarted;
        unsigned int coverRetryCount = 0;
        bool disconnectedPublished = false;
        auto observedSettingsVersion = settingsVersion_.load();

        while (!shutdown_)
        {
            const auto currentSettingsVersion = settingsVersion_.load();
            const auto settings = CurrentSettings();
            if (currentSettingsVersion != observedSettingsVersion)
            {
                observedSettingsVersion = currentSettingsVersion;
                previousTitle.clear();
                publishedTitle.clear();
                previousLyric.clear();
            }
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
            bool stateChanged = false;
            if (snapshot.windowTitle != previousTitle)
            {
                previousTitle = snapshot.windowTitle;
                track = catalog_.Find(snapshot.windowTitle);
                if (track.name.empty()) track = snapshot.track;
                trackStarted = std::chrono::steady_clock::now();
                lyrics = settings.showLyrics ? lyricsStore_.Find(track) : std::vector<LyricLine>{};
                cover = LoadCover(track.coverUrl);
                coverRetryCount = 0;
                coverRetryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
                previousLyric.clear();
                publishedTitle.clear();
            }

            const auto now = std::chrono::steady_clock::now();
            if (cover.empty() && now >= coverRetryAt && !snapshot.windowTitle.empty())
            {
                auto refreshed = catalog_.FindQueued(snapshot.windowTitle);
                if (refreshed.name.empty() && coverRetryCount > 0 && coverRetryCount % 6 == 0)
                {
                    refreshed = catalog_.Find(snapshot.windowTitle, true);
                }
                if (!refreshed.name.empty())
                {
                    const bool metadataChanged = refreshed.coverUrl != track.coverUrl
                        || refreshed.trackId != track.trackId || refreshed.lyricsId != track.lyricsId;
                    track = refreshed;
                    if (metadataChanged && settings.showLyrics)
                    {
                        lyrics = lyricsStore_.Find(track);
                    }
                }
                if (!track.coverUrl.empty())
                {
                    cover = LoadCover(track.coverUrl);
                    stateChanged = !cover.empty();
                }
                ++coverRetryCount;
                const auto delay = std::min(1000u, 100u * (coverRetryCount + 1));
                coverRetryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay);
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - trackStarted);
            const auto lyric = settings.showLyrics ? LyricsStore::Current(lyrics, elapsed) : std::wstring();
            if (stateChanged || lyric != previousLyric || snapshot.windowTitle != publishedTitle)
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
            Wait(std::chrono::milliseconds(settings.refreshIntervalMs));
        }

        settingsWindow_.Stop();
        if (CurrentSettings().closeCloudMusicOnExit)
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
        if (command == BandCommand::Options)
        {
            settingsWindow_.Show();
            return;
        }
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

    AppSettings CurrentSettings() const
    {
        std::lock_guard lock(settingsMutex_);
        return settings_;
    }

    void UpdateSettings(const AppSettings& settings)
    {
        {
            std::lock_guard lock(settingsMutex_);
            settings_ = settings;
        }
        ++settingsVersion_;
    }

    AppOptions options_;
    PlayerController player_;
    std::wstring settingsPath_;
    mutable std::mutex settingsMutex_;
    AppSettings settings_;
    SettingsWindow settingsWindow_;
    TrackCatalog catalog_;
    LyricsStore lyricsStore_;
    std::atomic_bool shutdown_{false};
    std::atomic<DWORD> processId_{0};
    std::atomic<std::uint64_t> settingsVersion_{0};
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
