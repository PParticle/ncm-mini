#include "Host.h"

#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <utility>

namespace ncmmini
{
namespace
{
constexpr UINT CloudMusicCommandCode = 0x1800;

struct WindowInfo
{
    HWND handle = nullptr;
    DWORD processId = 0;
    std::wstring className;
    std::wstring title;
    bool visible = false;
};

std::wstring ReadWindowString(HWND window, bool className)
{
    wchar_t buffer[1024]{};
    const auto length = className
        ? GetClassNameW(window, buffer, static_cast<int>(std::size(buffer)))
        : GetWindowTextW(window, buffer, static_cast<int>(std::size(buffer)));
    return length > 0 ? std::wstring(buffer, length) : std::wstring();
}

BOOL CALLBACK CollectWindows(HWND window, LPARAM parameter)
{
    auto* windows = reinterpret_cast<std::vector<WindowInfo>*>(parameter);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    windows->push_back({
        window,
        processId,
        ReadWindowString(window, true),
        ReadWindowString(window, false),
        IsWindowVisible(window) != FALSE
    });
    return TRUE;
}

std::vector<WindowInfo> EnumerateWindows()
{
    std::vector<WindowInfo> windows;
    EnumWindows(CollectWindows, reinterpret_cast<LPARAM>(&windows));
    return windows;
}

std::unordered_set<DWORD> CloudMusicProcessIds()
{
    std::unordered_set<DWORD> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return result;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szExeFile, L"cloudmusic.exe") == 0)
            {
                result.insert(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

std::wstring EnvironmentValue(const wchar_t* name)
{
    const auto length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0)
    {
        return {};
    }
    std::wstring value(length, L'\0');
    const auto written = GetEnvironmentVariableW(name, value.data(), length);
    if (written == 0)
    {
        return {};
    }
    value.resize(written);
    return value;
}

bool FileExists(const std::wstring& path)
{
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring Quote(const std::wstring& value)
{
    return L"\"" + value + L"\"";
}
}

AppOptions ParseOptions(int argumentCount, wchar_t** arguments)
{
    AppOptions options;
    for (int index = 1; index < argumentCount; ++index)
    {
        std::wstring argument = arguments[index];
        std::transform(argument.begin(), argument.end(), argument.begin(),
            [](wchar_t character) { return static_cast<wchar_t>(towlower(character)); });
        if ((argument == L"--cloudmusic" || argument == L"--player") && index + 1 < argumentCount)
        {
            options.cloudMusicPath = arguments[++index];
        }
        else if (argument == L"--no-launch")
        {
            options.launchCloudMusic = false;
        }
        else if (argument == L"--keep-player")
        {
            options.closeCloudMusicOnExit = false;
        }
        else if (argument == L"--no-lyrics")
        {
            options.showLyrics = false;
        }
    }
    return options;
}

std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
    {
        return {};
    }
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0)
    {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
    {
        return {};
    }
    const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0)
    {
        return {};
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::wstring Trim(std::wstring text)
{
    const auto first = std::find_if_not(text.begin(), text.end(), [](wchar_t character) { return iswspace(character) != 0; });
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](wchar_t character) { return iswspace(character) != 0; }).base();
    return first < last ? std::wstring(first, last) : std::wstring();
}

std::pair<std::wstring, std::wstring> ParsePlayerTitle(const std::wstring& title)
{
    const auto clean = Trim(title);
    const auto separator = clean.rfind(L" - ");
    if (separator == std::wstring::npos || separator == 0 || separator + 3 >= clean.size())
    {
        return {clean, {}};
    }
    return {Trim(clean.substr(0, separator)), Trim(clean.substr(separator + 3))};
}

std::wstring LocalAppDataPath()
{
    return EnvironmentValue(L"LOCALAPPDATA");
}

std::wstring ExecutableDirectory()
{
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    const auto separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? L"." : path.substr(0, separator);
}

void Log(const std::wstring& message)
{
    static std::mutex gate;
    std::lock_guard lock(gate);
    try
    {
        const std::filesystem::path directory = std::filesystem::path(LocalAppDataPath()) / L"NCM Mini";
        std::filesystem::create_directories(directory);
        SYSTEMTIME time{};
        GetLocalTime(&time);
        wchar_t prefix[64]{};
        swprintf(prefix, std::size(prefix), L"%04u-%02u-%02uT%02u:%02u:%02u.%03u ",
            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
        std::wofstream stream(directory / L"ncm-mini.log", std::ios::app);
        stream << prefix << message << L'\n';
    }
    catch (...)
    {
    }
}

bool RunBandController(const wchar_t* command)
{
    const auto path = std::filesystem::path(ExecutableDirectory()) / L"NCMMiniBandCtl.exe";
    if (!FileExists(path.wstring()))
    {
        Log(L"DeskBand controller was not found: " + path.wstring());
        return false;
    }
    std::wstring commandLine = Quote(path.wstring()) + L" " + command;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(path.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, path.parent_path().c_str(), &startup, &process))
    {
        Log(L"DeskBand controller failed: " + std::to_wstring(GetLastError()));
        return false;
    }
    WaitForSingleObject(process.hProcess, 3000);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exitCode == 0;
}

PlayerController::PlayerController(AppOptions options) : options_(std::move(options))
{
}

bool PlayerController::TryLaunch()
{
    if (ReadSnapshot().running)
    {
        return true;
    }
    const auto path = ResolvePath();
    if (path.empty())
    {
        Log(L"cloudmusic.exe was not found");
        return false;
    }
    std::wstring commandLine = Quote(path);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::filesystem::path executable(path);
    if (!CreateProcessW(path.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0,
        nullptr, executable.parent_path().c_str(), &startup, &process))
    {
        Log(L"failed to start CloudMusic: " + std::to_wstring(GetLastError()));
        return false;
    }
    launchedProcessId_ = process.dwProcessId;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    Log(L"started CloudMusic: " + path);
    return true;
}

PlayerSnapshot PlayerController::ReadSnapshot() const
{
    const auto processIds = CloudMusicProcessIds();
    if (processIds.empty())
    {
        return {};
    }
    const auto windows = EnumerateWindows();
    const WindowInfo* mainWindow = nullptr;
    for (const auto& candidate : windows)
    {
        if (processIds.count(candidate.processId) != 0 && candidate.className == L"OrpheusBrowserHost")
        {
            mainWindow = &candidate;
            break;
        }
    }
    if (mainWindow == nullptr)
    {
        for (const auto& candidate : windows)
        {
            if (processIds.count(candidate.processId) != 0 && candidate.className == L"icon")
            {
                mainWindow = &candidate;
                break;
            }
        }
    }
    if (mainWindow == nullptr)
    {
        PlayerSnapshot starting;
        starting.running = true;
        starting.processId = *processIds.begin();
        return starting;
    }

    const WindowInfo* titleWindow = nullptr;
    for (const auto& candidate : windows)
    {
        if (candidate.processId == mainWindow->processId
            && candidate.className == L"OrpheusBrowserHost" && !Trim(candidate.title).empty())
        {
            titleWindow = &candidate;
            break;
        }
    }
    if (titleWindow == nullptr)
    {
        for (const auto& candidate : windows)
        {
            if (candidate.processId == mainWindow->processId && candidate.className == L"icon" && !Trim(candidate.title).empty())
            {
                titleWindow = &candidate;
                break;
            }
        }
    }
    PlayerSnapshot snapshot;
    snapshot.running = true;
    snapshot.processId = mainWindow->processId;
    snapshot.mainWindow = mainWindow->handle;
    snapshot.windowTitle = titleWindow == nullptr ? std::wstring() : Trim(titleWindow->title);
    const auto [name, artist] = ParsePlayerTitle(snapshot.windowTitle);
    snapshot.track.name = name;
    snapshot.track.artist = artist;
    return snapshot;
}

bool PlayerController::Send(BandCommand command, DWORD processId) const
{
    int slot = -1;
    if (command == BandCommand::Previous) slot = 0;
    else if (command == BandCommand::PlayPause) slot = 1;
    else if (command == BandCommand::Next) slot = 2;
    if (slot < 0)
    {
        return false;
    }
    HWND target = nullptr;
    for (const auto& window : EnumerateWindows())
    {
        if (window.processId == processId && window.className == L"icon")
        {
            target = window.handle;
            break;
        }
    }
    if (target == nullptr)
    {
        return false;
    }
    const WPARAM parameter = MAKELONG(static_cast<WORD>(slot), static_cast<WORD>(CloudMusicCommandCode));
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(target, WM_COMMAND, parameter, 0, SMTO_ABORTIFHUNG, 1000, &result) != 0;
}

void PlayerController::Close()
{
    auto snapshot = ReadSnapshot();
    if (!snapshot.running)
    {
        return;
    }
    if (snapshot.mainWindow != nullptr)
    {
        PostMessageW(snapshot.mainWindow, WM_CLOSE, 0, 0);
    }
    for (int attempt = 0; attempt < 30; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!ReadSnapshot().running)
        {
            return;
        }
    }
    for (const auto processId : CloudMusicProcessIds())
    {
        HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, processId);
        if (process != nullptr)
        {
            TerminateProcess(process, 0);
            CloseHandle(process);
        }
    }
}

std::wstring PlayerController::ResolvePath() const
{
    const std::vector<std::wstring> candidates = {
        options_.cloudMusicPath,
        EnvironmentValue(L"NETEASE_CLOUDMUSIC_PATH"),
        L"D:\\Apps\\Netease\\CloudMusic\\cloudmusic.exe",
        LocalAppDataPath() + L"\\NetEase\\CloudMusic\\cloudmusic.exe",
        EnvironmentValue(L"ProgramFiles") + L"\\NetEase\\CloudMusic\\cloudmusic.exe",
        EnvironmentValue(L"ProgramFiles(x86)") + L"\\NetEase\\CloudMusic\\cloudmusic.exe"
    };
    for (const auto& candidate : candidates)
    {
        if (!candidate.empty() && FileExists(candidate))
        {
            return candidate;
        }
    }
    return {};
}
}
