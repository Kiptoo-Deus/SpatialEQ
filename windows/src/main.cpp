// SpatialEQ for Windows: Win32 window, Direct3D 11 + Dear ImGui rendering, notification-area icon.
//
//   SpatialEQ.exe [--minimized]
//
// --minimized starts in the notification area (used by "Start with Windows").
#include "App.h"
#include "Ui.h"
#include "resource.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

constexpr wchar_t kWindowClass[] = L"SpatialEQWindow";
constexpr UINT WM_TRAY = WM_APP + 2;
constexpr UINT WM_DEVICES_CHANGED = WM_APP + 1; // posted by DeviceList
constexpr UINT kTrayId = 1;
enum : UINT { kMenuOpen = 100, kMenuEnable, kMenuQuit };

ID3D11Device* gDevice = nullptr;
ID3D11DeviceContext* gContext = nullptr;
IDXGISwapChain* gSwapChain = nullptr;
ID3D11RenderTargetView* gTarget = nullptr;
UINT gResizeW = 0, gResizeH = 0;

sq::app::App* gApp = nullptr;
sq::app::UiFonts gFonts;
float gScale = 1.0f;
bool gRebuildFonts = false;
bool gQuitting = false;
NOTIFYICONDATAW gTray{};

void createTarget() {
    ID3D11Texture2D* back = nullptr;
    gSwapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        gDevice->CreateRenderTargetView(back, nullptr, &gTarget);
        back->Release();
    }
}

void releaseTarget() {
    if (gTarget) {
        gTarget->Release();
        gTarget = nullptr;
    }
}

bool createDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &gSwapChain, &gDevice, &got, &gContext);
    if (hr == DXGI_ERROR_UNSUPPORTED) // no GPU driver (e.g. some VMs): fall back to WARP
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &sd, &gSwapChain, &gDevice, &got, &gContext);
    if (FAILED(hr)) return false;
    createTarget();
    return true;
}

void destroyDevice() {
    releaseTarget();
    if (gSwapChain) gSwapChain->Release();
    if (gContext) gContext->Release();
    if (gDevice) gDevice->Release();
    gSwapChain = nullptr;
    gContext = nullptr;
    gDevice = nullptr;
}

void loadFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0x2190, 0x2193, 0};
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    auto load = [&](const char* file, float size) -> ImFont* {
        std::string path = "C:\\Windows\\Fonts\\";
        path += file;
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
        return io.Fonts->AddFontFromFileTTF(path.c_str(), std::round(size * scale), &cfg, kRanges);
    };
    gFonts.body = load("segoeui.ttf", 15.5f);
    gFonts.caption = load("segoeui.ttf", 13.0f);
    gFonts.title = load("segoeuib.ttf", 17.0f);
    if (!gFonts.body) {
        ImFontConfig def;
        def.SizePixels = std::round(13 * scale);
        gFonts.body = gFonts.caption = gFonts.title = io.Fonts->AddFontDefault(&def);
    }
    if (!gFonts.caption) gFonts.caption = gFonts.body;
    if (!gFonts.title) gFonts.title = gFonts.body;
    io.FontDefault = gFonts.body;
}

void applyScale(float scale) {
    gScale = scale;
    sq::app::setupStyle(scale);
    loadFonts(scale);
    ImGui_ImplDX11_InvalidateDeviceObjects(); // font atlas is rebuilt on the next frame
}

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
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
    static UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (msg == taskbarCreated) { // Explorer restarted: re-add the tray icon
        Shell_NotifyIconW(NIM_ADD, &gTray);
        return 0;
    }
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            gResizeW = LOWORD(lParam);
            gResizeH = HIWORD(lParam);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lParam);
        mm->ptMinTrackSize = {LONG(1060 * gScale), LONG(700 * gScale)};
        return 0;
    }
    case WM_DPICHANGED: {
        gRebuildFonts = true;
        gScale = HIWORD(wParam) / 96.0f;
        const RECT* r = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
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
    case WM_DEVICES_CHANGED: return 0; // just wakes the loop; App::tick handles it
    case WM_ENDSESSION:
        // Logging off or shutting down: no WM_QUIT will reach the loop, so restore app volumes now.
        if (wParam && gApp) gApp->shutdown();
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0; // no Alt menu beep
        break;
    case WM_DESTROY:
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
    ImGui_ImplWin32_EnableDpiAwareness();
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), 0));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(9, 10, 15));
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    // Size the window for the primary monitor's DPI, centred.
    const float scale0 = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY));
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int w = std::min(int(1440 * scale0), int(work.right - work.left) - 40);
    const int h = std::min(int(900 * scale0), int(work.bottom - work.top) - 40);
    HWND hwnd = CreateWindowExW(0, kWindowClass, L"SpatialEQ", WS_OVERLAPPEDWINDOW, work.left + (work.right - work.left - w) / 2,
                                work.top + (work.bottom - work.top - h) / 2, w, h, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    // Dark title bar (Windows 10 20H1+ / 11); ignored where unsupported.
    {
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) {
            using SetAttr = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
            if (auto set = reinterpret_cast<SetAttr>(GetProcAddress(dwm, "DwmSetWindowAttribute"))) {
                const BOOL dark = TRUE;
                if (FAILED(set(hwnd, 20, &dark, sizeof(dark)))) set(hwnd, 19, &dark, sizeof(dark));
            }
            FreeLibrary(dwm);
        }
    }

    if (!createDevice(hwnd)) {
        destroyDevice();
        MessageBoxW(hwnd, L"Direct3D 11 is not available on this computer.", L"SpatialEQ", MB_ICONERROR);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(gDevice, gContext);
    applyScale(ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd));

    auto appPtr = std::make_unique<sq::app::App>();
    sq::app::App& app = *appPtr;
    gApp = &app;
    app.start(hwnd);
    addTrayIcon(hwnd);
    if (!startMinimized) {
        ShowWindow(hwnd, SW_SHOWDEFAULT);
        UpdateWindow(hwnd);
    }

    bool running = true;
    while (running) {
        const bool visible = IsWindowVisible(hwnd) && !IsIconic(hwnd);
        if (!visible) {
            // Hidden in the tray: keep app logic ticking (device changes, autosave) without rendering.
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
        }
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        app.tick();
        if (!visible) continue;

        if (gRebuildFonts) {
            gRebuildFonts = false;
            applyScale(gScale);
        }
        if (gResizeW != 0 && gResizeH != 0) {
            releaseTarget();
            gSwapChain->ResizeBuffers(0, gResizeW, gResizeH, DXGI_FORMAT_UNKNOWN, 0);
            gResizeW = gResizeH = 0;
            createTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        sq::app::drawUi(app, gFonts, gScale, hwnd);
        ImGui::Render();

        const float clear[4] = {0.035f, 0.04f, 0.06f, 1.0f};
        gContext->OMSetRenderTargets(1, &gTarget, nullptr);
        gContext->ClearRenderTargetView(gTarget, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const HRESULT hr = gSwapChain->Present(1, 0);
        if (hr == DXGI_STATUS_OCCLUDED) Sleep(16);
    }

    app.shutdown();
    gApp = nullptr;
    appPtr.reset(); // releases COM objects before CoUninitialize
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroyDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(kWindowClass, inst);
    CoUninitialize();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}
