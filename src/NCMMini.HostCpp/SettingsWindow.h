#pragma once

#include "../Common/Settings.h"

#include <functional>
#include <string>
#include <thread>

namespace ncmmini
{
class SettingsWindow
{
public:
    using ApplyHandler = std::function<void(const AppSettings&)>;

    SettingsWindow(std::wstring path, ApplyHandler applyHandler);
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    bool Start();
    void Show();
    void Stop();

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void Run();
    void CreateControls(HWND window);
    void ApplyControlFont(HWND window) const;
    void LoadControls();
    bool ReadControls();
    bool Apply();
    void ChooseFontSetting();
    void ChooseColorSetting(int index);
    void DrawColorButton(const DRAWITEMSTRUCT& item) const;
    void CenterWindow() const;
    void BringToForeground() const;

    std::wstring path_;
    ApplyHandler applyHandler_;
    AppSettings working_;
    HANDLE readyEvent_ = nullptr;
    DWORD threadId_ = 0;
    HWND window_ = nullptr;
    HFONT controlFont_ = nullptr;
    std::thread thread_;
};
}
