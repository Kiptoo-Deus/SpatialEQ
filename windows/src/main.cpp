// SpatialEQ for Windows: Win32 window hosting the web interface (the same one as the Mac-matching VST3
// editor, see WebUi.h), notification-area icon.
//
//   SpatialEQ.exe [--minimized]
//
// --minimized starts in the notification area (used by "Start with Windows").
#include "App.h"
#include "WebUi.h"
#include "resource.h"

#include <windows.h>

#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cstring>
#include <memory>

namespace {

constexpr wchar_t kWindowClass[] = L"SpatialEQWindow";
constexpr UINT WM_TRAY = WM_APP + 2;
constexpr UINT WM_DEVICES_CHANGED = WM_APP + 1; // posted by DeviceList
constexpr UINT kTrayId = 1;
constexpr UINT_PTR kTickTimer = 1;
constexpr UINT kTickMs = 33; // the spectrum and meters update about 30 times a second
enum : UINT { kMenuOpen = 100, kMenuEnable, kMenuQuit };

sq::app::App* gApp = nullptr;
sq::app::WebUi* gUi = nullptr;
bool gQuitting = false;
NOTIFYICONDATAW gTray{};

void addTrayIcon(HWND hwnd) {
    gTray.cbSize = sizeof(gTray);
    gTray.hWnd = hwnd;
    gTray.uID = kTrayId;
    gTray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    gTray.uCallbackMessage = WM_TRAY;
    gTray.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    wcscpy_s(gTray.szTip, L"SpatialEQ");
    Shell_NotifyIconW(NIM_ADD, &gTray);
}

void showWindow(HWND hwnd) {
    ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd);
}

void trayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuOpen, L"Open SpatialEQ");
    AppendMenuW(menu, MF_STRING | (gApp && gApp->enabled ? MF_CHECKED : 0), kMenuEnable, L"Processing enabled");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit");
    SetMenuDefaultItem(menu, kMenuOpen, FALSE);
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd); // required so the menu closes when clicking elsewhere
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
    case kMenuOpen: showWindow(hwnd); break;
    case kMenuEnable:
        if (gApp) gApp->enabled = !gApp->enabled;
        break;
    case kMenuQuit:
        gQuitting = true;
        DestroyWindow(hwnd);
        break;
    default: break;
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (msg == taskbarCreated) { // Explorer restarted: re-add the tray icon
        Shell_NotifyIconW(NIM_ADD, &gTray);
        return 0;
    }
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED && gUi) gUi->resize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_GETMINMAXINFO: {
        // The same minimum as the Mac window (1080 x 720 points).
        const float scale = GetDpiForWindow(hwnd) / 96.0f;
        auto* mm = reinterpret_cast<MINMAXINFO*>(lParam);
        mm->ptMinTrackSize = {LONG(1080 * scale), LONG(720 * scale)};
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT* r = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_TIMER:
        if (wParam == kTickTimer && gApp) {
            gApp->tick(); // device changes, route restarts, parameters, metering, autosave
            if (gUi && IsWindowVisible(hwnd) && !IsIconic(hwnd)) gUi->tick();
        }
        return 0;
    case WM_CLOSE:
        if (gApp && gApp->closeToTray && !gQuitting) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_TRAY:
        if (LOWORD(lParam) == WM_LBUTTONUP || LOWORD(lParam) == WM_LBUTTONDBLCLK) showWindow(hwnd);
        else if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) trayMenu(hwnd);
        return 0;
    case WM_DEVICES_CHANGED: return 0; // App::tick picks the change up
    case WM_ENDSESSION:
        // Logging off or shutting down: no WM_QUIT will reach the loop, so restore app volumes now.
        if (wParam && gApp) gApp->shutdown();
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kTickTimer);
        Shell_NotifyIconW(NIM_DELETE, &gTray);
        PostQuitMessage(0);
        return 0;
    case WM_COPYDATA: // a second instance asked us to show ourselves
        showWindow(hwnd);
        return TRUE;
    default: break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    // One instance per user: a second launch brings the running one to the front.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\SpatialEQ.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kWindowClass, nullptr)) {
            COPYDATASTRUCT cds{};
            SendMessageW(other, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds));
        }
        return 0;
    }

    const bool startMinimized = cmdLine && std::wcsstr(cmdLine, L"--minimized");
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), 0));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(9, 10, 15)); // the UI's background, shown while it loads
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    // The Mac window's default size (1280 x 820 points), for the primary monitor's DPI, centred.
    const float scale0 = GetDpiForSystem() / 96.0f;
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT frame{0, 0, LONG(1280 * scale0), LONG(820 * scale0)};
    AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem());
    const int w = std::min(int(frame.right - frame.left), int(work.right - work.left) - 40);
    const int h = std::min(int(frame.bottom - frame.top), int(work.bottom - work.top) - 40);
    HWND hwnd = CreateWindowExW(0, kWindowClass, L"SpatialEQ", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - h) / 2,
                                w, h, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    // Dark title bar (Windows 10 20H1+ / 11); ignored where unsupported.
    const BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));

    auto appPtr = std::make_unique<sq::app::App>();
    sq::app::App& app = *appPtr;
    gApp = &app;
    app.start(hwnd);
    auto ui = std::make_unique<sq::app::WebUi>(app, hwnd);
    gUi = ui.get();
    addTrayIcon(hwnd);
    SetTimer(hwnd, kTickTimer, kTickMs, nullptr);
    if (!startMinimized) {
        ShowWindow(hwnd, SW_SHOWDEFAULT);
        UpdateWindow(hwnd);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    app.shutdown();
    gUi = nullptr;
    ui.reset();
    gApp = nullptr;
    appPtr.reset(); // releases COM objects before CoUninitialize
    UnregisterClassW(kWindowClass, inst);
    CoUninitialize();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}
