#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <string>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace xmf::theme {

inline bool IsSystemDarkMode() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme",
            RRF_RT_REG_DWORD,
            nullptr,
            &value,
            &size) == ERROR_SUCCESS) {
        return value == 0;
    }
    return true;
}

inline void ApplyDarkWindowFrame(HWND hwnd) {
    if (!hwnd) return;
    BOOL useDark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));

    // Rounded corners on Windows 11
    DWORD cornerPref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));

    // Dark caption & border color
    COLORREF captionColor = RGB(24, 24, 28);
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
    COLORREF borderColor = RGB(52, 52, 60);
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

    // Enable dark context menu / scrollbars for the process where supported
    using fnSetPreferredAppMode = int(WINAPI*)(int);
    static fnSetPreferredAppMode pSetPreferredAppMode = []() -> fnSetPreferredAppMode {
        HMODULE hUx = GetModuleHandleW(L"uxtheme.dll");
        if (!hUx) hUx = LoadLibraryW(L"uxtheme.dll");
        if (!hUx) return nullptr;
        return reinterpret_cast<fnSetPreferredAppMode>(GetProcAddress(hUx, MAKEINTRESOURCEA(135)));
    }();
    if (pSetPreferredAppMode) {
        pSetPreferredAppMode(2); // ForceDark
    }
}

inline void ApplyDarkControlTheme(HWND hwnd) {
    if (!hwnd) return;
    SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
}

inline HBRUSH DarkBgBrush() {
    static HBRUSH brush = CreateSolidBrush(RGB(24, 24, 28));
    return brush;
}

inline HBRUSH DarkSurfaceBrush() {
    static HBRUSH brush = CreateSolidBrush(RGB(34, 34, 40));
    return brush;
}

} // namespace xmf::theme
