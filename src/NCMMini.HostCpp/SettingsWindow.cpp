#include "SettingsWindow.h"

#include <commdlg.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <utility>

namespace ncmmini
{
namespace
{
constexpr wchar_t SettingsWindowClass[] = L"NCMMini.Settings.Window.1";
constexpr UINT ShowWindowMessage = WM_APP + 1;
constexpr UINT StopWindowMessage = WM_APP + 2;
constexpr int WindowWidth = 550;
constexpr int WindowHeight = 365;

enum ControlId
{
    ShowLyricsId = 101,
    FontDisplayId,
    FontChooseId,
    TitleColorId,
    ButtonColorId,
    LyricColorId,
    RefreshIntervalId,
    CloseCloudMusicId,
    ConfirmId,
    CancelId,
    ApplyId
};

HWND AddControl(HWND parent, const wchar_t* className, const wchar_t* text, DWORD style,
    int x, int y, int width, int height, int id = 0, DWORD extendedStyle = 0)
{
    return CreateWindowExW(extendedStyle, className, text, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
}

std::wstring FontDescription(const FontSettings& font)
{
    return font.name + L"  " + std::to_wstring(font.pointSize) + L" \u78c5";
}

void SetCheck(HWND window, int id, bool checked)
{
    CheckDlgButton(window, id, checked ? BST_CHECKED : BST_UNCHECKED);
}

bool GetCheck(HWND window, int id)
{
    return IsDlgButtonChecked(window, id) == BST_CHECKED;
}

void SetInteger(HWND window, int id, int value)
{
    SetDlgItemInt(window, id, static_cast<UINT>(value), FALSE);
}

int ReadInteger(HWND window, int id, int minimum, int maximum, const wchar_t* name, bool& valid)
{
    BOOL converted = FALSE;
    const auto value = static_cast<int>(GetDlgItemInt(window, id, &converted, FALSE));
    if (!converted || value < minimum || value > maximum)
    {
        const auto message = std::wstring(name) + L"\u5fc5\u987b\u4ecb\u4e8e " + std::to_wstring(minimum)
            + L" \u5230 " + std::to_wstring(maximum) + L" \u4e4b\u95f4\u3002";
        MessageBoxW(window, message.c_str(), L"NCM Mini", MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(window, id));
        SendDlgItemMessageW(window, id, EM_SETSEL, 0, -1);
        valid = false;
        return minimum;
    }
    return value;
}

COLORREF* ColorByIndex(AppSettings& settings, int index)
{
    switch (index)
    {
    case 0: return &settings.titleColor;
    case 1: return &settings.buttonColor;
    case 2: return &settings.lyricColor;
    default: return nullptr;
    }
}
}

SettingsWindow::SettingsWindow(std::wstring path, ApplyHandler applyHandler)
    : path_(std::move(path)), applyHandler_(std::move(applyHandler))
{
    readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

SettingsWindow::~SettingsWindow()
{
    Stop();
    if (readyEvent_ != nullptr) CloseHandle(readyEvent_);
}

bool SettingsWindow::Start()
{
    if (thread_.joinable()) return window_ != nullptr;
    ResetEvent(readyEvent_);
    thread_ = std::thread(&SettingsWindow::Run, this);
    return WaitForSingleObject(readyEvent_, 5000) == WAIT_OBJECT_0 && window_ != nullptr;
}

void SettingsWindow::Show()
{
    if (window_ != nullptr) PostMessageW(window_, ShowWindowMessage, 0, 0);
}

void SettingsWindow::Stop()
{
    if (!thread_.joinable()) return;
    if (window_ != nullptr) PostMessageW(window_, StopWindowMessage, 0, 0);
    else if (threadId_ != 0) PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
    thread_.join();
}

void SettingsWindow::Run()
{
    threadId_ = GetCurrentThreadId();
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = SettingsWindowClass;
    RegisterClassExW(&windowClass);

    RECT rectangle{0, 0, WindowWidth, WindowHeight};
    AdjustWindowRectEx(&rectangle, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    window_ = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        SettingsWindowClass,
        L"\u9009\u9879\u8bbe\u7f6e",
        WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rectangle.right - rectangle.left,
        rectangle.bottom - rectangle.top,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        this);
    SetEvent(readyEvent_);
    if (window_ == nullptr) return;
    working_ = LoadSettings(path_);
    LoadControls();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageW(window_, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    window_ = nullptr;
    DeleteObject(controlFont_);
    controlFont_ = nullptr;
    UnregisterClassW(SettingsWindowClass, GetModuleHandleW(nullptr));
}

LRESULT CALLBACK SettingsWindow::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SettingsWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self == nullptr ? DefWindowProcW(window, message, wParam, lParam)
        : self->HandleMessage(window, message, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        CreateControls(window);
        return 0;
    case ShowWindowMessage:
        working_ = LoadSettings(path_);
        LoadControls();
        CenterWindow();
        ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOWNORMAL);
        BringToForeground();
        return 0;
    case StopWindowMessage:
        DestroyWindow(window);
        return 0;
    case WM_CLOSE:
        ShowWindow(window, SW_HIDE);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case FontChooseId:
            ChooseFontSetting();
            return 0;
        case TitleColorId:
        case ButtonColorId:
        case LyricColorId:
            ChooseColorSetting(LOWORD(wParam) - TitleColorId);
            return 0;
        case ConfirmId:
            if (Apply()) ShowWindow(window, SW_HIDE);
            return 0;
        case CancelId:
            ShowWindow(window, SW_HIDE);
            return 0;
        case ApplyId:
            Apply();
            return 0;
        }
        break;
    case WM_DRAWITEM:
        if (wParam >= TitleColorId && wParam <= LyricColorId)
        {
            DrawColorButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        }
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void SettingsWindow::CreateControls(HWND window)
{
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    controlFont_ = CreateFontIndirectW(&metrics.lfMessageFont);

    AddControl(window, L"BUTTON", L"\u663e\u793a\u5185\u5bb9", BS_GROUPBOX, 12, 10, 526, 56);
    AddControl(window, L"BUTTON", L"\u663e\u793a\u5b9e\u65f6\u6b4c\u8bcd", BS_AUTOCHECKBOX | WS_TABSTOP, 28, 31, 150, 22, ShowLyricsId);

    AddControl(window, L"BUTTON", L"\u5b57\u4f53", BS_GROUPBOX, 12, 73, 526, 65);
    AddControl(window, L"STATIC", L"\u5b57\u4f53\uff1a", SS_LEFT, 28, 99, 56, 20);
    AddControl(window, L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL | WS_BORDER, 86, 94, 334, 24, FontDisplayId, WS_EX_CLIENTEDGE);
    AddControl(window, L"BUTTON", L"\u9009\u62e9...", BS_PUSHBUTTON | WS_TABSTOP, 432, 93, 86, 26, FontChooseId);

    AddControl(window, L"BUTTON", L"\u989c\u8272", BS_GROUPBOX, 12, 145, 526, 83);
    AddControl(window, L"STATIC", L"\u6807\u9898\uff1a", SS_LEFT, 28, 173, 56, 20);
    AddControl(window, L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, 86, 167, 92, 25, TitleColorId);
    AddControl(window, L"STATIC", L"\u6309\u94ae\uff1a", SS_LEFT, 196, 173, 56, 20);
    AddControl(window, L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, 254, 167, 92, 25, ButtonColorId);
    AddControl(window, L"STATIC", L"\u6b4c\u8bcd\uff1a", SS_LEFT, 364, 173, 56, 20);
    AddControl(window, L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, 422, 167, 92, 25, LyricColorId);
    AddControl(window, L"STATIC", L"\u6807\u9898\u4f7f\u7528\u7c97\u4f53\uff0c\u6b4c\u624b\u4f7f\u7528\u540c\u8272\u6de1\u5316\u7684\u5e38\u89c4\u5b57\u4f53\u3002", SS_LEFT, 28, 203, 360, 18);

    AddControl(window, L"BUTTON", L"\u884c\u4e3a\u4e0e\u66f4\u65b0", BS_GROUPBOX, 12, 235, 526, 76);
    AddControl(window, L"STATIC", L"\u5a92\u4f53\u5237\u65b0\u95f4\u9694\uff1a", SS_LEFT, 28, 260, 106, 20);
    AddControl(window, L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 134, 257, 58, 23, RefreshIntervalId, WS_EX_CLIENTEDGE);
    AddControl(window, L"STATIC", L"\u6beb\u79d2 (50-2000)", SS_LEFT, 200, 260, 125, 20);
    AddControl(window, L"BUTTON", L"\u9000\u51fa NCM Mini \u65f6\u540c\u65f6\u5173\u95ed\u7f51\u6613\u4e91\u97f3\u4e50", BS_AUTOCHECKBOX | WS_TABSTOP,
        28, 283, 310, 22, CloseCloudMusicId);

    AddControl(window, L"BUTTON", L"\u786e\u5b9a", BS_DEFPUSHBUTTON | WS_TABSTOP, 268, 325, 82, 28, ConfirmId);
    AddControl(window, L"BUTTON", L"\u53d6\u6d88", BS_PUSHBUTTON | WS_TABSTOP, 360, 325, 82, 28, CancelId);
    AddControl(window, L"BUTTON", L"\u5e94\u7528", BS_PUSHBUTTON | WS_TABSTOP, 452, 325, 82, 28, ApplyId);
    ApplyControlFont(window);
}

void SettingsWindow::ApplyControlFont(HWND window) const
{
    EnumChildWindows(window, [](HWND child, LPARAM font) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(controlFont_));
}

void SettingsWindow::LoadControls()
{
    SetCheck(window_, ShowLyricsId, working_.showLyrics);
    SetCheck(window_, CloseCloudMusicId, working_.closeCloudMusicOnExit);
    SetInteger(window_, RefreshIntervalId, working_.refreshIntervalMs);
    SetDlgItemTextW(window_, FontDisplayId, FontDescription(working_.font).c_str());
    for (int id = TitleColorId; id <= LyricColorId; ++id)
    {
        InvalidateRect(GetDlgItem(window_, id), nullptr, TRUE);
    }
}

bool SettingsWindow::ReadControls()
{
    bool valid = true;
    const auto refresh = ReadInteger(window_, RefreshIntervalId, 50, 2000, L"\u5a92\u4f53\u5237\u65b0\u95f4\u9694", valid);
    if (!valid) return false;
    working_.showLyrics = GetCheck(window_, ShowLyricsId);
    working_.closeCloudMusicOnExit = GetCheck(window_, CloseCloudMusicId);
    working_.refreshIntervalMs = refresh;
    return true;
}

bool SettingsWindow::Apply()
{
    if (!ReadControls()) return false;
    if (!SaveSettings(path_, working_))
    {
        MessageBoxW(window_, L"\u65e0\u6cd5\u4fdd\u5b58 config.ini\uff0c\u8bf7\u68c0\u67e5\u6587\u4ef6\u6743\u9650\u3002", L"NCM Mini", MB_OK | MB_ICONERROR);
        return false;
    }
    if (applyHandler_) applyHandler_(working_);
    NotifyDeskBandSettingsChanged();
    return true;
}

void SettingsWindow::ChooseFontSetting()
{
    auto& font = working_.font;
    LOGFONTW logFont{};
    const HDC device = GetDC(window_);
    const auto dpi = GetDeviceCaps(device, LOGPIXELSY);
    ReleaseDC(window_, device);
    logFont.lfHeight = -MulDiv(font.pointSize, dpi, 72);
    logFont.lfWeight = FW_NORMAL;
    wcsncpy(logFont.lfFaceName, font.name.c_str(), LF_FACESIZE - 1);
    logFont.lfFaceName[LF_FACESIZE - 1] = L'\0';
    CHOOSEFONTW choice{};
    choice.lStructSize = sizeof(choice);
    choice.hwndOwner = window_;
    choice.lpLogFont = &logFont;
    choice.iPointSize = font.pointSize * 10;
    choice.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_FORCEFONTEXIST | CF_NOSTYLESEL;
    if (!ChooseFontW(&choice)) return;
    font.name = logFont.lfFaceName;
    font.pointSize = std::clamp(static_cast<int>(choice.iPointSize / 10), 6, 24);
    SetDlgItemTextW(window_, FontDisplayId, FontDescription(font).c_str());
}

void SettingsWindow::ChooseColorSetting(int index)
{
    auto* color = ColorByIndex(working_, index);
    if (color == nullptr) return;
    static std::array<COLORREF, 16> customColors{};
    CHOOSECOLORW choice{};
    choice.lStructSize = sizeof(choice);
    choice.hwndOwner = window_;
    choice.rgbResult = *color;
    choice.lpCustColors = customColors.data();
    choice.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&choice)) return;
    *color = choice.rgbResult;
    InvalidateRect(GetDlgItem(window_, TitleColorId + index), nullptr, TRUE);
}

void SettingsWindow::DrawColorButton(const DRAWITEMSTRUCT& item) const
{
    auto settings = working_;
    auto* color = ColorByIndex(settings, static_cast<int>(item.CtlID) - TitleColorId);
    if (color == nullptr) return;
    RECT rectangle = item.rcItem;
    HBRUSH brush = CreateSolidBrush(*color);
    FillRect(item.hDC, &rectangle, brush);
    DeleteObject(brush);
    DrawEdge(item.hDC, &rectangle, (item.itemState & ODS_SELECTED) ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
    wchar_t text[16]{};
    swprintf(text, std::size(text), L"#%02X%02X%02X", GetRValue(*color), GetGValue(*color), GetBValue(*color));
    const auto brightness = GetRValue(*color) * 299 + GetGValue(*color) * 587 + GetBValue(*color) * 114;
    SetTextColor(item.hDC, brightness > 140000 ? RGB(0, 0, 0) : RGB(255, 255, 255));
    SetBkMode(item.hDC, TRANSPARENT);
    SelectObject(item.hDC, controlFont_);
    DrawTextW(item.hDC, text, -1, &rectangle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if ((item.itemState & ODS_FOCUS) != 0)
    {
        InflateRect(&rectangle, -3, -3);
        DrawFocusRect(item.hDC, &rectangle);
    }
}

void SettingsWindow::CenterWindow() const
{
    RECT windowRect{};
    RECT workArea{};
    GetWindowRect(window_, &windowRect);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    const auto monitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);
    if (monitor != nullptr && GetMonitorInfoW(monitor, &monitorInfo))
    {
        workArea = monitorInfo.rcWork;
    }
    else
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    }
    const auto width = windowRect.right - windowRect.left;
    const auto height = windowRect.bottom - windowRect.top;
    const auto x = workArea.left + (workArea.right - workArea.left - width) / 2;
    const auto y = workArea.top + (workArea.bottom - workArea.top - height) / 2;
    SetWindowPos(window_, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void SettingsWindow::BringToForeground() const
{
    const auto foreground = GetForegroundWindow();
    const auto currentThread = GetCurrentThreadId();
    const auto foregroundThread = foreground == nullptr
        ? 0
        : GetWindowThreadProcessId(foreground, nullptr);
    const bool attached = foregroundThread != 0 && foregroundThread != currentThread
        && AttachThreadInput(currentThread, foregroundThread, TRUE) != FALSE;

    SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    BringWindowToTop(window_);
    SetForegroundWindow(window_);
    SetActiveWindow(window_);
    SetFocus(window_);
    SetWindowPos(window_, HWND_NOTOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

    if (attached)
    {
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    }
}
}
