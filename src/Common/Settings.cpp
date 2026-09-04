#include "Settings.h"

#include <algorithm>
#include <cwchar>

namespace ncmmini
{
namespace
{
int ReadInteger(const std::wstring& path, const wchar_t* section, const wchar_t* key, int fallback, int minimum, int maximum)
{
    const auto value = static_cast<int>(GetPrivateProfileIntW(section, key, fallback, path.c_str()));
    return std::clamp(value, minimum, maximum);
}

bool ReadBoolean(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool fallback)
{
    return ReadInteger(path, section, key, fallback ? 1 : 0, 0, 1) != 0;
}

std::wstring ReadString(const std::wstring& path, const wchar_t* section, const wchar_t* key, const std::wstring& fallback)
{
    wchar_t value[256]{};
    GetPrivateProfileStringW(section, key, fallback.c_str(), value, static_cast<DWORD>(std::size(value)), path.c_str());
    return value;
}

COLORREF ReadColor(const std::wstring& path, const wchar_t* key, COLORREF fallback)
{
    const auto value = ReadString(path, L"Colors", key, L"");
    int red = -1;
    int green = -1;
    int blue = -1;
    if (swscanf(value.c_str(), L"%d,%d,%d", &red, &green, &blue) != 3
        || red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255)
    {
        return fallback;
    }
    return RGB(red, green, blue);
}

bool WriteValue(const std::wstring& path, const wchar_t* section, const wchar_t* key, const std::wstring& value)
{
    return WritePrivateProfileStringW(section, key, value.c_str(), path.c_str()) != FALSE;
}

bool WriteInteger(const std::wstring& path, const wchar_t* section, const wchar_t* key, int value)
{
    return WriteValue(path, section, key, std::to_wstring(value));
}

bool WriteBoolean(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool value)
{
    return WriteInteger(path, section, key, value ? 1 : 0);
}

bool WriteColor(const std::wstring& path, const wchar_t* key, COLORREF color)
{
    const auto value = std::to_wstring(GetRValue(color)) + L"," + std::to_wstring(GetGValue(color))
        + L"," + std::to_wstring(GetBValue(color));
    return WriteValue(path, L"Colors", key, value);
}

BOOL CALLBACK NotifyChild(HWND window, LPARAM)
{
    wchar_t className[128]{};
    GetClassNameW(window, className, static_cast<int>(std::size(className)));
    if (wcscmp(className, BandWindowClassName) == 0)
    {
        PostMessageW(window, ReloadSettingsMessageId, 0, 0);
    }
    return TRUE;
}

BOOL CALLBACK NotifyTopLevel(HWND window, LPARAM)
{
    NotifyChild(window, 0);
    EnumChildWindows(window, NotifyChild, 0);
    return TRUE;
}
}

std::wstring SettingsPath(HMODULE module)
{
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
    {
        return L"config.ini";
    }
    path.resize(length);
    const auto separator = path.find_last_of(L"\\/");
    return (separator == std::wstring::npos ? std::wstring() : path.substr(0, separator + 1)) + L"config.ini";
}

AppSettings LoadSettings(const std::wstring& path)
{
    AppSettings settings;
    settings.showLyrics = ReadBoolean(path, L"Display", L"ShowLyrics", settings.showLyrics);

    const auto legacyFontName = ReadString(path, L"TitleFont", L"Name", settings.font.name);
    const auto legacyFontSize = ReadInteger(path, L"TitleFont", L"Size", settings.font.pointSize, 6, 24);
    settings.font.name = ReadString(path, L"Font", L"Name", legacyFontName);
    settings.font.pointSize = ReadInteger(path, L"Font", L"Size", legacyFontSize, 6, 24);

    settings.titleColor = ReadColor(path, L"Title", settings.titleColor);
    settings.lyricColor = ReadColor(path, L"Lyric", settings.lyricColor);
    settings.buttonColor = ReadColor(path, L"Button", settings.buttonColor);
    settings.refreshIntervalMs = ReadInteger(path, L"Behavior", L"RefreshIntervalMs", settings.refreshIntervalMs, 50, 2000);
    settings.closeCloudMusicOnExit = ReadBoolean(path, L"Behavior", L"CloseCloudMusicOnExit", settings.closeCloudMusicOnExit);
    return settings;
}

bool SaveSettings(const std::wstring& path, const AppSettings& settings)
{
    bool result = true;
    result &= WriteBoolean(path, L"Display", L"ShowLyrics", settings.showLyrics);
    result &= WriteValue(path, L"Font", L"Name", settings.font.name);
    result &= WriteInteger(path, L"Font", L"Size", settings.font.pointSize);
    result &= WriteColor(path, L"Title", settings.titleColor);
    result &= WriteColor(path, L"Lyric", settings.lyricColor);
    result &= WriteColor(path, L"Button", settings.buttonColor);
    result &= WriteInteger(path, L"Behavior", L"RefreshIntervalMs", settings.refreshIntervalMs);
    result &= WriteBoolean(path, L"Behavior", L"CloseCloudMusicOnExit", settings.closeCloudMusicOnExit);
    WritePrivateProfileStringW(L"Display", L"ShowCover", nullptr, path.c_str());
    WritePrivateProfileStringW(L"Display", L"CoverInset", nullptr, path.c_str());
    WritePrivateProfileStringW(L"Display", L"ButtonSize", nullptr, path.c_str());
    WritePrivateProfileStringW(L"TitleFont", nullptr, nullptr, path.c_str());
    WritePrivateProfileStringW(L"DetailFont", nullptr, nullptr, path.c_str());
    WritePrivateProfileStringW(L"Colors", L"Artist", nullptr, path.c_str());
    WritePrivateProfileStringW(L"Colors", L"ButtonHover", nullptr, path.c_str());
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    return result;
}

void NotifyDeskBandSettingsChanged()
{
    EnumWindows(NotifyTopLevel, 0);
}
}
