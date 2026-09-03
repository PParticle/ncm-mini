#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>

#include <cwchar>
#include <cstdio>

static const CLSID CLSID_NCMMiniDeskBand =
{ 0x9c941a4d, 0xd554, 0x4012, { 0x88, 0xe0, 0x95, 0x31, 0xd6, 0xb8, 0x80, 0xba } };

int wmain(int argumentCount, wchar_t** arguments)
{
    if (argumentCount != 2)
    {
        return 2;
    }
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized))
    {
        return 3;
    }

    ITrayDeskBand* tray = nullptr;
    auto result = CoCreateInstance(CLSID_TrayDeskBand, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&tray));
    std::fwprintf(stderr, L"CoCreateInstance=0x%08lX\n", static_cast<unsigned long>(result));
    if (SUCCEEDED(result))
    {
        if (_wcsicmp(arguments[1], L"show") == 0)
        {
            tray->DeskBandRegistrationChanged();
            result = tray->IsDeskBandShown(CLSID_NCMMiniDeskBand);
            std::fwprintf(stderr, L"IsDeskBandShown=0x%08lX\n", static_cast<unsigned long>(result));
            if (result != S_OK)
            {
                result = tray->ShowDeskBand(CLSID_NCMMiniDeskBand);
                std::fwprintf(stderr, L"ShowDeskBand=0x%08lX\n", static_cast<unsigned long>(result));
            }
        }
        else if (_wcsicmp(arguments[1], L"hide") == 0)
        {
            result = tray->IsDeskBandShown(CLSID_NCMMiniDeskBand);
            std::fwprintf(stderr, L"IsDeskBandShown=0x%08lX\n", static_cast<unsigned long>(result));
            if (result == S_OK)
            {
                result = tray->HideDeskBand(CLSID_NCMMiniDeskBand);
            }
            else if (result == S_FALSE)
            {
                result = S_OK;
            }
        }
        else if (_wcsicmp(arguments[1], L"refresh") == 0)
        {
            result = tray->DeskBandRegistrationChanged();
        }
        else if (_wcsicmp(arguments[1], L"status") == 0)
        {
            result = tray->IsDeskBandShown(CLSID_NCMMiniDeskBand);
        }
        else
        {
            result = E_INVALIDARG;
        }
        tray->Release();
    }
    CoUninitialize();
    return SUCCEEDED(result) ? 0 : 1;
}
