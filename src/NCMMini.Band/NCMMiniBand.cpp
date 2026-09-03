#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <ole2.h>
#include <shobjidl.h>
#include <strsafe.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Protocol.h"

static HMODULE moduleHandle = nullptr;
static std::atomic_long serverLocks{0};
static INIT_ONCE windowClassOnce = INIT_ONCE_STATIC_INIT;

static const CLSID CLSID_NCMMiniDeskBand =
{ 0x9c941a4d, 0xd554, 0x4012, { 0x88, 0xe0, 0x95, 0x31, 0xd6, 0xb8, 0x80, 0xba } };

static constexpr wchar_t ClsidText[] = L"{9C941A4D-D554-4012-88E0-9531D6B880BA}";
static constexpr wchar_t DeskBandCategoryText[] = L"{00021492-0000-0000-C000-000000000046}";
static constexpr wchar_t WindowClassName[] = L"NCMMini.DeskBand.Window.1";
static constexpr UINT StateMessageId = WM_APP + 41;
static constexpr UINT ConnectionMessageId = WM_APP + 42;
static constexpr UINT ExitMenuId = 1001;
static constexpr std::size_t CoverByteCount = 40 * 40 * 4;

struct PlayerState
{
    bool running = false;
    std::wstring title;
    std::wstring artist = L"\u6b63\u5728\u8fde\u63a5";
    std::wstring lyric;
    std::vector<std::uint8_t> cover;
};

static std::wstring Utf8ToWide(const char* data, std::size_t length)
{
    if (length == 0)
    {
        return {};
    }
    const auto required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, static_cast<int>(length), nullptr, 0);
    if (required <= 0)
    {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, static_cast<int>(length), result.data(), required);
    return result;
}

class DeskBand final : public IDeskBand, public IObjectWithSite, public IInputObject, public IPersistStream
{
public:
    DeskBand() : references_(1)
    {
        ++serverLocks;
    }

    ~DeskBand()
    {
        CloseDW(0);
        if (site_ != nullptr)
        {
            site_->Release();
        }
        DeleteObject(titleFont_);
        DeleteObject(detailFont_);
        --serverLocks;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
    {
        if (object == nullptr)
        {
            return E_POINTER;
        }
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IOleWindow || iid == IID_IDockingWindow || iid == IID_IDeskBand)
        {
            *object = static_cast<IDeskBand*>(this);
        }
        else if (iid == IID_IObjectWithSite)
        {
            *object = static_cast<IObjectWithSite*>(this);
        }
        else if (iid == IID_IInputObject)
        {
            *object = static_cast<IInputObject*>(this);
        }
        else if (iid == IID_IPersist || iid == IID_IPersistStream)
        {
            *object = static_cast<IPersistStream*>(this);
        }
        else
        {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++references_;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const auto references = --references_;
        if (references == 0)
        {
            delete this;
        }
        return references;
    }

    HRESULT STDMETHODCALLTYPE GetWindow(HWND* window) override
    {
        if (window == nullptr)
        {
            return E_POINTER;
        }
        *window = window_;
        return window_ == nullptr ? E_FAIL : S_OK;
    }

    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE ShowDW(BOOL show) override
    {
        if (window_ != nullptr)
        {
            ShowWindow(window_, show ? SW_SHOW : SW_HIDE);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CloseDW(DWORD) override
    {
        StopWorker();
        if (window_ != nullptr)
        {
            MSG message{};
            while (PeekMessageW(&message, window_, StateMessageId, StateMessageId, PM_REMOVE))
            {
                delete reinterpret_cast<PlayerState*>(message.lParam);
            }
            DestroyWindow(window_);
            window_ = nullptr;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ResizeBorderDW(LPCRECT, IUnknown*, BOOL) override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetBandInfo(DWORD, DWORD, DESKBANDINFO* info) override
    {
        if (info == nullptr)
        {
            return E_POINTER;
        }
        if ((info->dwMask & DBIM_MINSIZE) != 0)
        {
            info->ptMinSize = { 330, 32 };
        }
        if ((info->dwMask & DBIM_MAXSIZE) != 0)
        {
            info->ptMaxSize = { 620, -1 };
        }
        if ((info->dwMask & DBIM_INTEGRAL) != 0)
        {
            info->ptIntegral = { 1, 1 };
        }
        if ((info->dwMask & DBIM_ACTUAL) != 0)
        {
            info->ptActual = { 420, 40 };
        }
        if ((info->dwMask & DBIM_TITLE) != 0)
        {
            info->wszTitle[0] = L'\0';
        }
        if ((info->dwMask & DBIM_MODEFLAGS) != 0)
        {
            info->dwModeFlags = DBIMF_VARIABLEHEIGHT | DBIMF_NORMAL;
        }
        if ((info->dwMask & DBIM_BKCOLOR) != 0)
        {
            info->dwMask &= ~DBIM_BKCOLOR;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) override
    {
        CloseDW(0);
        if (site_ != nullptr)
        {
            site_->Release();
            site_ = nullptr;
        }
        if (site == nullptr)
        {
            return S_OK;
        }

        site_ = site;
        site_->AddRef();
        IOleWindow* oleWindow = nullptr;
        auto result = site_->QueryInterface(IID_PPV_ARGS(&oleWindow));
        if (FAILED(result))
        {
            return result;
        }
        HWND parent = nullptr;
        result = oleWindow->GetWindow(&parent);
        oleWindow->Release();
        if (FAILED(result) || parent == nullptr)
        {
            return FAILED(result) ? result : E_FAIL;
        }

        InitOnceExecuteOnce(&windowClassOnce, RegisterWindowClass, nullptr, nullptr);
        CreateFonts(parent);
        window_ = CreateWindowExW(
            0,
            WindowClassName,
            L"NCM Mini",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0,
            0,
            420,
            40,
            parent,
            nullptr,
            moduleHandle,
            this);
        if (window_ == nullptr)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        StartWorker();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSite(REFIID iid, void** site) override
    {
        if (site == nullptr)
        {
            return E_POINTER;
        }
        *site = nullptr;
        return site_ == nullptr ? E_FAIL : site_->QueryInterface(iid, site);
    }

    HRESULT STDMETHODCALLTYPE UIActivateIO(BOOL, MSG*) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE HasFocusIO() override
    {
        return S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE TranslateAcceleratorIO(MSG*) override
    {
        return S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* clsid) override
    {
        if (clsid == nullptr)
        {
            return E_POINTER;
        }
        *clsid = CLSID_NCMMiniDeskBand;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE IsDirty() override
    {
        return S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Load(IStream*) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Save(IStream*, BOOL) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSizeMax(ULARGE_INTEGER* size) override
    {
        if (size == nullptr)
        {
            return E_POINTER;
        }
        size->QuadPart = 0;
        return S_OK;
    }

private:
    static BOOL CALLBACK RegisterWindowClass(PINIT_ONCE, PVOID, PVOID*)
    {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.hInstance = moduleHandle;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = WindowClassName;
        return RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* self = reinterpret_cast<DeskBand*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<DeskBand*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self == nullptr ? DefWindowProcW(window, message, wParam, lParam) : self->HandleMessage(window, message, wParam, lParam);
    }

    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_PAINT:
            Paint(window);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_MOUSEMOVE:
            TrackMouse(window, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSELEAVE:
            mouseTracking_ = false;
            hoverButton_ = -1;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            Click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_RBUTTONUP:
            ShowContextMenu(window, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_SETCURSOR:
            if (hoverButton_ >= 0)
            {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case StateMessageId:
        {
            std::unique_ptr<PlayerState> update(reinterpret_cast<PlayerState*>(lParam));
            if (update)
            {
                std::lock_guard lock(stateMutex_);
                state_ = std::move(*update);
            }
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case ConnectionMessageId:
            connected_ = wParam != 0;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void CreateFonts(HWND parent)
    {
        DeleteObject(titleFont_);
        DeleteObject(detailFont_);
        HDC device = GetDC(parent);
        const auto dpi = GetDeviceCaps(device, LOGPIXELSY);
        ReleaseDC(parent, device);
        titleFont_ = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        detailFont_ = CreateFontW(-MulDiv(8, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    }

    void Paint(HWND window)
    {
        PAINTSTRUCT paint{};
        HDC target = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        const auto width = std::max(1L, client.right);
        const auto height = std::max(1L, client.bottom);
        HDC buffer = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
        const auto previousBitmap = SelectObject(buffer, bitmap);

        if (FAILED(DrawThemeParentBackground(window, buffer, &client)))
        {
            HBRUSH background = CreateSolidBrush(GetSysColor(COLOR_3DFACE));
            FillRect(buffer, &client, background);
            DeleteObject(background);
        }

        PlayerState state;
        {
            std::lock_guard lock(stateMutex_);
            state = state_;
        }

        const auto inset = 2;
        const auto coverSize = std::max(1L, std::min(40L, height - inset * 2));
        RECT coverRect{ inset, (height - coverSize) / 2, inset + coverSize, (height + coverSize) / 2 };
        DrawCover(buffer, coverRect, state.cover);

        const auto buttonSize = std::max(1L, std::min(32L, height - inset * 2));
        const auto buttonsWidth = buttonSize * 3;
        const auto buttonTop = (height - buttonSize) / 2;
        for (int index = 0; index < 3; ++index)
        {
            buttonRects_[index] = {
                width - buttonsWidth + buttonSize * index,
                buttonTop,
                width - buttonsWidth + buttonSize * (index + 1),
                buttonTop + buttonSize
            };
            DrawButton(buffer, buttonRects_[index], index, state.running);
        }

        RECT textRect{ coverRect.right + 7, 1, width - buttonsWidth - 6, height - 1 };
        if (textRect.right > textRect.left)
        {
            const auto middle = textRect.top + (textRect.bottom - textRect.top) / 2;
            RECT titleRect{ textRect.left, textRect.top, textRect.right, middle + 1 };
            RECT detailRect{ textRect.left, middle - 1, textRect.right, textRect.bottom };
            SetBkMode(buffer, TRANSPARENT);
            SetTextColor(buffer, state.running ? RGB(242, 242, 244) : RGB(185, 185, 188));
            SelectObject(buffer, titleFont_);
            DrawTextW(buffer, state.title.c_str(), -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SetTextColor(buffer, state.lyric.empty() ? RGB(178, 179, 184) : RGB(126, 211, 174));
            SelectObject(buffer, detailFont_);
            const auto& detail = state.lyric.empty() ? state.artist : state.lyric;
            DrawTextW(buffer, detail.c_str(), -1, &detailRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        BitBlt(target, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, previousBitmap);
        DeleteObject(bitmap);
        DeleteDC(buffer);
        EndPaint(window, &paint);
    }

    static void DrawCover(HDC device, const RECT& destination, const std::vector<std::uint8_t>& cover)
    {
        HBRUSH placeholder = CreateSolidBrush(RGB(54, 55, 60));
        FillRect(device, &destination, placeholder);
        DeleteObject(placeholder);
        if (cover.size() != CoverByteCount)
        {
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(110, 111, 117));
            const auto previousPen = SelectObject(device, pen);
            MoveToEx(device, destination.left + 11, destination.top + 9, nullptr);
            LineTo(device, destination.left + 11, destination.bottom - 8);
            Ellipse(device, destination.left + 5, destination.bottom - 13, destination.left + 13, destination.bottom - 5);
            LineTo(device, destination.right - 8, destination.top + 6);
            LineTo(device, destination.right - 8, destination.bottom - 11);
            Ellipse(device, destination.right - 14, destination.bottom - 16, destination.right - 6, destination.bottom - 8);
            SelectObject(device, previousPen);
            DeleteObject(pen);
            return;
        }

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 40;
        info.bmiHeader.biHeight = -40;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        StretchDIBits(device,
            destination.left,
            destination.top,
            destination.right - destination.left,
            destination.bottom - destination.top,
            0,
            0,
            40,
            40,
            cover.data(),
            &info,
            DIB_RGB_COLORS,
            SRCCOPY);
    }

    void DrawButton(HDC device, const RECT& rectangle, int index, bool enabled) const
    {
        const auto active = enabled && hoverButton_ == index;
        if (active)
        {
            HBRUSH brush = CreateSolidBrush(RGB(62, 63, 68));
            FillRect(device, &rectangle, brush);
            DeleteObject(brush);
        }
        const auto color = enabled ? RGB(235, 235, 238) : RGB(105, 105, 108);
        HPEN pen = CreatePen(PS_SOLID, 2, color);
        HBRUSH iconBrush = CreateSolidBrush(color);
        const auto oldPen = SelectObject(device, pen);
        const auto oldBrush = SelectObject(device, iconBrush);
        const auto centerX = (rectangle.left + rectangle.right) / 2;
        const auto centerY = (rectangle.top + rectangle.bottom) / 2;

        if (index == 0 || index == 2)
        {
            const auto direction = index == 0 ? -1 : 1;
            const auto barX = centerX + direction * 8;
            MoveToEx(device, barX, centerY - 7, nullptr);
            LineTo(device, barX, centerY + 7);
            POINT first[] = {
                { centerX + direction * 6, centerY },
                { centerX - direction * 1, centerY - 7 },
                { centerX - direction * 1, centerY + 7 }
            };
            Polygon(device, first, 3);
            POINT second[] = {
                { centerX - direction * 1, centerY },
                { centerX - direction * 8, centerY - 7 },
                { centerX - direction * 8, centerY + 7 }
            };
            Polygon(device, second, 3);
        }
        else
        {
            Rectangle(device, centerX - 8, centerY - 7, centerX - 5, centerY + 7);
            Rectangle(device, centerX - 3, centerY - 7, centerX, centerY + 7);
            POINT triangle[] = {
                { centerX + 3, centerY - 7 },
                { centerX + 10, centerY },
                { centerX + 3, centerY + 7 }
            };
            Polygon(device, triangle, 3);
        }

        SelectObject(device, oldBrush);
        SelectObject(device, oldPen);
        DeleteObject(iconBrush);
        DeleteObject(pen);
    }

    void TrackMouse(HWND window, int x, int y)
    {
        if (!mouseTracking_)
        {
            TRACKMOUSEEVENT tracking{ sizeof(tracking), TME_LEAVE, window, 0 };
            TrackMouseEvent(&tracking);
            mouseTracking_ = true;
        }
        const auto previous = hoverButton_;
        hoverButton_ = ButtonAt(x, y);
        if (previous != hoverButton_)
        {
            InvalidateRect(window, nullptr, FALSE);
        }
    }

    int ButtonAt(int x, int y) const
    {
        POINT point{ x, y };
        for (int index = 0; index < 3; ++index)
        {
            if (PtInRect(&buttonRects_[index], point))
            {
                return index;
            }
        }
        return -1;
    }

    void Click(int x, int y)
    {
        bool running;
        {
            std::lock_guard lock(stateMutex_);
            running = state_.running;
        }
        if (!running)
        {
            return;
        }
        const auto button = ButtonAt(x, y);
        if (button >= 0)
        {
            QueueCommand(static_cast<ncmmini::Command>(button + 1));
        }
    }

    void ShowContextMenu(HWND window, int x, int y)
    {
        POINT location{ x, y };
        ClientToScreen(window, &location);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, ExitMenuId, L"\u9000\u51fa NCM Mini");
        const auto selection = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
            location.x, location.y, 0, window, nullptr);
        DestroyMenu(menu);
        if (selection == ExitMenuId)
        {
            QueueCommand(ncmmini::Command::Exit);
        }
    }

    void QueueCommand(ncmmini::Command command)
    {
        {
            std::lock_guard lock(commandMutex_);
            commands_.push_back(command);
        }
        workerWake_.notify_one();
    }

    void StartWorker()
    {
        stopWorker_ = false;
        worker_ = std::thread([this] { WorkerLoop(); });
    }

    void StopWorker()
    {
        stopWorker_ = true;
        workerWake_.notify_all();
        if (worker_.joinable())
        {
            worker_.join();
        }
        connected_ = false;
    }

    void WorkerLoop()
    {
        while (!stopWorker_)
        {
            HANDLE pipe = CreateFileW(ncmmini::PipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE)
            {
                std::unique_lock lock(commandMutex_);
                workerWake_.wait_for(lock, std::chrono::milliseconds(250), [this] { return stopWorker_.load(); });
                continue;
            }
            PostConnection(true);
            if (!ProcessPipe(pipe))
            {
                CloseHandle(pipe);
                PostConnection(false);
                continue;
            }
            CloseHandle(pipe);
            PostConnection(false);
        }
    }

    bool ProcessPipe(HANDLE pipe)
    {
        while (!stopWorker_)
        {
            std::deque<ncmmini::Command> commands;
            {
                std::lock_guard lock(commandMutex_);
                commands.swap(commands_);
            }
            for (const auto command : commands)
            {
                ncmmini::CommandPacket packet{
                    ncmmini::ProtocolMagic,
                    ncmmini::ProtocolVersion,
                    ncmmini::CommandMessage,
                    static_cast<std::uint32_t>(command)
                };
                DWORD written = 0;
                if (!WriteFile(pipe, &packet, sizeof(packet), &written, nullptr) || written != sizeof(packet))
                {
                    return false;
                }
            }

            ncmmini::StateHeader header{};
            DWORD headerBytes = 0;
            DWORD available = 0;
            if (!PeekNamedPipe(pipe, &header, sizeof(header), &headerBytes, &available, nullptr))
            {
                return false;
            }
            if (available >= sizeof(header) && headerBytes == sizeof(header))
            {
                if (header.magic != ncmmini::ProtocolMagic
                    || header.version != ncmmini::ProtocolVersion
                    || header.message != ncmmini::StateMessage)
                {
                    return false;
                }
                const auto bodyBytes = static_cast<std::uint64_t>(header.titleBytes)
                    + header.artistBytes + header.lyricBytes + header.coverBytes;
                const auto packetBytes = sizeof(header) + bodyBytes;
                if (bodyBytes > 1024 * 1024)
                {
                    return false;
                }
                if (available >= packetBytes)
                {
                    std::vector<std::uint8_t> packet(static_cast<std::size_t>(packetBytes));
                    DWORD read = 0;
                    if (!ReadFile(pipe, packet.data(), static_cast<DWORD>(packet.size()), &read, nullptr) || read != packet.size())
                    {
                        return false;
                    }
                    PublishState(packet);
                    continue;
                }
            }

            std::unique_lock lock(commandMutex_);
            workerWake_.wait_for(lock, std::chrono::milliseconds(50), [this] { return stopWorker_ || !commands_.empty(); });
        }
        return true;
    }

    void PublishState(const std::vector<std::uint8_t>& packet)
    {
        const auto* header = reinterpret_cast<const ncmmini::StateHeader*>(packet.data());
        const auto* cursor = reinterpret_cast<const char*>(packet.data() + sizeof(*header));
        auto state = std::make_unique<PlayerState>();
        state->running = (header->flags & 1) != 0;
        state->title = Utf8ToWide(cursor, header->titleBytes);
        cursor += header->titleBytes;
        state->artist = Utf8ToWide(cursor, header->artistBytes);
        cursor += header->artistBytes;
        state->lyric = Utf8ToWide(cursor, header->lyricBytes);
        cursor += header->lyricBytes;
        if (header->coverBytes == CoverByteCount)
        {
            state->cover.assign(reinterpret_cast<const std::uint8_t*>(cursor), reinterpret_cast<const std::uint8_t*>(cursor) + CoverByteCount);
        }
        auto* update = state.release();
        if (window_ == nullptr || !PostMessageW(window_, StateMessageId, 0, reinterpret_cast<LPARAM>(update)))
        {
            delete update;
        }
    }

    void PostConnection(bool connected) const
    {
        if (window_ != nullptr)
        {
            PostMessageW(window_, ConnectionMessageId, connected ? 1 : 0, 0);
        }
    }

    std::atomic_ulong references_;
    IUnknown* site_ = nullptr;
    HWND window_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT detailFont_ = nullptr;
    RECT buttonRects_[3]{};
    bool mouseTracking_ = false;
    int hoverButton_ = -1;
    bool connected_ = false;
    std::mutex stateMutex_;
    PlayerState state_;
    std::atomic_bool stopWorker_{false};
    std::thread worker_;
    std::mutex commandMutex_;
    std::condition_variable workerWake_;
    std::deque<ncmmini::Command> commands_;
};

class ClassFactory final : public IClassFactory
{
public:
    ClassFactory() : references_(1)
    {
        ++serverLocks;
    }

    ~ClassFactory()
    {
        --serverLocks;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
    {
        if (object == nullptr)
        {
            return E_POINTER;
        }
        *object = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory)
        {
            return E_NOINTERFACE;
        }
        *object = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++references_;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const auto references = --references_;
        if (references == 0)
        {
            delete this;
        }
        return references;
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** object) override
    {
        if (outer != nullptr)
        {
            return CLASS_E_NOAGGREGATION;
        }
        auto* deskBand = new (std::nothrow) DeskBand();
        if (deskBand == nullptr)
        {
            return E_OUTOFMEMORY;
        }
        const auto result = deskBand->QueryInterface(iid, object);
        deskBand->Release();
        return result;
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override
    {
        lock ? ++serverLocks : --serverLocks;
        return S_OK;
    }

private:
    std::atomic_ulong references_;
};

static HRESULT SetStringValue(HKEY root, const wchar_t* path, const wchar_t* name, const wchar_t* value)
{
    HKEY key = nullptr;
    const auto result = RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS)
    {
        return HRESULT_FROM_WIN32(result);
    }
    const auto bytes = static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t));
    const auto valueResult = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value), bytes);
    RegCloseKey(key);
    return HRESULT_FROM_WIN32(valueResult);
}

extern "C" HRESULT __declspec(dllexport) WINAPI DllRegisterServer()
{
    wchar_t modulePath[MAX_PATH]{};
    if (GetModuleFileNameW(moduleHandle, modulePath, ARRAYSIZE(modulePath)) == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    wchar_t clsidPath[160]{};
    StringCchPrintfW(clsidPath, ARRAYSIZE(clsidPath), L"Software\\Classes\\CLSID\\%s", ClsidText);
    RegDeleteTreeW(HKEY_CURRENT_USER, clsidPath);
    auto result = SetStringValue(HKEY_CURRENT_USER, clsidPath, nullptr, L"NCM Mini DeskBand");
    if (FAILED(result))
    {
        return result;
    }
    wchar_t inprocPath[200]{};
    StringCchPrintfW(inprocPath, ARRAYSIZE(inprocPath), L"%s\\InprocServer32", clsidPath);
    result = SetStringValue(HKEY_CURRENT_USER, inprocPath, nullptr, modulePath);
    if (FAILED(result))
    {
        return result;
    }
    result = SetStringValue(HKEY_CURRENT_USER, inprocPath, L"ThreadingModel", L"Apartment");
    if (FAILED(result))
    {
        return result;
    }
    wchar_t categoryPath[260]{};
    StringCchPrintfW(categoryPath, ARRAYSIZE(categoryPath), L"%s\\Implemented Categories\\%s", clsidPath, DeskBandCategoryText);
    result = SetStringValue(HKEY_CURRENT_USER, categoryPath, nullptr, L"");
    if (FAILED(result))
    {
        return result;
    }
    return SetStringValue(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
        ClsidText,
        L"NCM Mini DeskBand");
}

extern "C" HRESULT __declspec(dllexport) WINAPI DllUnregisterServer()
{
    wchar_t clsidPath[160]{};
    StringCchPrintfW(clsidPath, ARRAYSIZE(clsidPath), L"Software\\Classes\\CLSID\\%s", ClsidText);
    auto result = RegDeleteTreeW(HKEY_CURRENT_USER, clsidPath);
    HKEY approved = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
        0,
        KEY_SET_VALUE,
        &approved) == ERROR_SUCCESS)
    {
        RegDeleteValueW(approved, ClsidText);
        RegCloseKey(approved);
    }
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(result);
}

extern "C" HRESULT __declspec(dllexport) WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** object)
{
    if (object == nullptr)
    {
        return E_POINTER;
    }
    *object = nullptr;
    if (clsid != CLSID_NCMMiniDeskBand)
    {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    auto* factory = new (std::nothrow) ClassFactory();
    if (factory == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    const auto result = factory->QueryInterface(iid, object);
    factory->Release();
    return result;
}

extern "C" HRESULT __declspec(dllexport) WINAPI DllCanUnloadNow()
{
    return serverLocks.load() == 0 ? S_OK : S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        moduleHandle = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
