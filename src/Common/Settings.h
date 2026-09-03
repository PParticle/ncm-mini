#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace ncmmini
{
constexpr wchar_t BandWindowClassName[] = L"NCMMini.DeskBand.Window.1";
constexpr UINT ReloadSettingsMessageId = WM_APP + 43;

struct FontSettings
{
    std::wstring name = L"Microsoft YaHei UI";
    int pointSize = 9;
    int weight = FW_NORMAL;
    bool italic = false;
};

struct AppSettings
{
    bool showCover = true;
    bool showLyrics = true;
    int coverInset = 2;
    int buttonSize = 32;
    FontSettings titleFont{L"Microsoft YaHei UI", 9, FW_SEMIBOLD, false};
    FontSettings detailFont{L"Microsoft YaHei UI", 8, FW_NORMAL, false};
    COLORREF titleColor = RGB(242, 242, 244);
    COLORREF artistColor = RGB(178, 179, 184);
    COLORREF lyricColor = RGB(126, 211, 174);
    COLORREF buttonColor = RGB(190, 191, 195);
    COLORREF buttonHoverColor = RGB(255, 255, 255);
    int refreshIntervalMs = 100;
    bool closeCloudMusicOnExit = true;
};

std::wstring SettingsPath(HMODULE module = nullptr);
AppSettings LoadSettings(const std::wstring& path);
bool SaveSettings(const std::wstring& path, const AppSettings& settings);
void NotifyDeskBandSettingsChanged();
}
