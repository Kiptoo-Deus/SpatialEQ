#include "filedialog.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>

#include <string>

namespace sq {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string narrow(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(size_t(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

} // namespace

std::string chooseFile(void* parentWindow, bool save, const std::string& title, const std::string& defaultName) {
    wchar_t file[MAX_PATH] = {};
    const std::wstring name = widen(defaultName);
    wcsncpy_s(file, name.c_str(), _TRUNCATE);
    const std::wstring caption = widen(title);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = GetAncestor(static_cast<HWND>(parentWindow), GA_ROOT);
    ofn.lpstrFilter = L"Text files (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = caption.c_str();
    ofn.lpstrDefExt = L"txt";
    if (save) {
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&ofn)) return {};
    } else {
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&ofn)) return {};
    }
    return narrow(file);
}

void embedChildView(void* parent, void* child, int width, int height) {
    HWND hwnd = static_cast<HWND>(child);
    if (!parent || !hwnd) return;
    // choc creates the web view as a popup window; turn it into a child of the host's window.
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style = (style & ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME)) | WS_CHILD | WS_VISIBLE;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    SetParent(hwnd, static_cast<HWND>(parent));
    SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

void resizeChildView(void* child, int width, int height) {
    if (HWND hwnd = static_cast<HWND>(child))
        SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
}

void removeChildView(void* child) {
    if (HWND hwnd = static_cast<HWND>(child)) ShowWindow(hwnd, SW_HIDE); // choc destroys it
}

} // namespace sq
