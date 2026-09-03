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
    settings.showCover = ReadBoolean(path, L"Display", L"ShowCover", settings.showCover);
    settings.showLyrics = ReadBoolean(path, L"Display", L"ShowLyrics", settings.showLyrics);
    settings.coverInset = ReadInteger(path, L"Display", L"CoverInset", settings.coverInset, 0, 8);
    settings.buttonSize = ReadInteger(path, L"Display", L"ButtonSize", settings.buttonSize, 24, 40);

    settings.titleFont.name = ReadString(path, L"TitleFont", L"Name", settings.titleFont.name);
    settings.titleFont.pointSize = ReadInteger(path, L"TitleFont", L"Size", settings.titleFont.pointSize, 6, 24);
    settings.titleFont.weight = ReadInteger(path, L"TitleFont", L"Weight", settings.titleFont.weight, 100, 900);
    settings.titleFont.italic = ReadBoolean(path, L"TitleFont", L"Italic", settings.titleFont.italic);
    settings.detailFont.name = ReadString(path, L"DetailFont", L"Name", settings.detailFont.name);
    settings.detailFont.pointSize = ReadInteger(path, L"DetailFont", L"Size", settings.detailFont.pointSize, 6, 24);
    settings.detailFont.weight = ReadInteger(path, L"DetailFont", L"Weight", settings.detailFont.weight, 100, 900);
    settings.detailFont.italic = ReadBoolean(path, L"DetailFont", L"Italic", settings.detailFont.italic);

    settings.titleColor = ReadColor(path, L"Title", settings.titleColor);
    settings.artistColor = ReadColor(path, L"Artist", settings.artistColor);
    settings.lyricColor = ReadColor(path, L"Lyric", settings.lyricColor);
    settings.buttonColor = ReadColor(path, L"Button", settings.buttonColor);
    settings.buttonHoverColor = ReadColor(path, L"ButtonHover", settings.buttonHoverColor);
    settings.refreshIntervalMs = ReadInteger(path, L"Behavior", L"RefreshIntervalMs", settings.refreshIntervalMs, 50, 2000);
    settings.closeCloudMusicOnExit = ReadBoolean(path, L"Behavior", L"CloseCloudMusicOnExit", settings.closeCloudMusicOnExit);
    return settings;
}

bool SaveSettings(const std::wstring& path, const AppSettings& settings)
{
    bool result = true;
    result &= WriteBoolean(path, L"Display", L"ShowCover", settings.showCover);
    result &= WriteBoolean(path, L"Display", L"ShowLyrics", settings.showLyrics);
    result &= WriteInteger(path, L"Display", L"CoverInset", settings.coverInset);
    result &= WriteInteger(path, L"Display", L"ButtonSize", settings.buttonSize);
    result &= WriteValue(path, L"TitleFont", L"Name", settings.titleFont.name);
    result &= WriteInteger(path, L"TitleFont", L"Size", settings.titleFont.pointSize);
    result &= WriteInteger(path, L"TitleFont", L"Weight", settings.titleFont.weight);
    result &= WriteBoolean(path, L"TitleFont", L"Italic", settings.titleFont.italic);
    result &= WriteValue(path, L"DetailFont", L"Name", settings.detailFont.name);
    result &= WriteInteger(path, L"DetailFont", L"Size", settings.detailFont.pointSize);
    result &= WriteInteger(path, L"DetailFont", L"Weight", settings.detailFont.weight);
    result &= WriteBoolean(path, L"DetailFont", L"Italic", settings.detailFont.italic);
    result &= WriteColor(path, L"Title", settings.titleColor);
    result &= WriteColor(path, L"Artist", settings.artistColor);
    result &= WriteColor(path, L"Lyric", settings.lyricColor);
    result &= WriteColor(path, L"Button", settings.buttonColor);
    result &= WriteColor(path, L"ButtonHover", settings.buttonHoverColor);
    result &= WriteInteger(path, L"Behavior", L"RefreshIntervalMs", settings.refreshIntervalMs);
    result &= WriteBoolean(path, L"Behavior", L"CloseCloudMusicOnExit", settings.closeCloudMusicOnExit);
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    return result;
}

void NotifyDeskBandSettingsChanged()
{
    EnumWindows(NotifyTopLevel, 0);
}
}
