#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include "config.hpp"

namespace edgenotes {

// 8 pastel paper colors matching the browser app
inline const COLORREF PaperColors[8] = {
    RGB(185, 228, 208), // 0: Mint
    RGB(191, 220, 245), // 1: Sky
    RGB(220, 203, 241), // 2: Lavender
    RGB(248, 233, 165), // 3: Yellow
    RGB(245, 203, 213), // 4: Rose
    RGB(250, 211, 170), // 5: Peach
    RGB(184, 225, 226), // 6: Aqua
    RGB(224, 230, 237)  // 7: Paper
};

inline const wchar_t* PaperColorNames[8] = {
    L"Mint", L"Sky", L"Lavender", L"Yellow", L"Rose", L"Peach", L"Aqua", L"Paper"
};

// Ink colors for text on pastel paper
inline const COLORREF PaperInk = RGB(42, 57, 55);         // Primary ink
inline const COLORREF PaperMuted = RGB(88, 108, 104);      // Muted / metadata ink
inline const COLORREF PaperBorder = RGB(165, 185, 180);    // Subtle border

// Dark theme colors (for Deck & Manager)
inline const COLORREF NightSurface = RGB(27, 28, 30);      // Main background #1b1c1e
inline const COLORREF NightPanel = RGB(36, 38, 42);        // Secondary panel #24262a
inline const COLORREF NightBorder = RGB(56, 58, 64);       // Border #383a40
inline const COLORREF NightInk = RGB(236, 240, 242);       // Main light text
inline const COLORREF NightMuted = RGB(154, 162, 170);     // Secondary text
inline const COLORREF NightSelected = RGB(45, 68, 62);     // Selected item highlight

inline Gdiplus::Color ToGdiColor(COLORREF cr, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(cr), GetGValue(cr), GetBValue(cr));
}

inline bool IsSystemDarkTheme() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 1;
        DWORD size = sizeof(val);
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return val == 0;
        }
        RegCloseKey(hKey);
    }
    return false;
}

inline void ApplyDwmDarkAndCorners(HWND hwnd, bool dark) {
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20
    BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &value, sizeof(value));

    // DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2
    DWORD corners = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33, &corners, sizeof(corners));
}

// GDI+ drawing helper for smooth rounded rectangle path
inline void AddRoundedRectToPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& r, float rad) {
    if (rad <= 0.0f) {
        path.AddRectangle(r);
        return;
    }
    float dia = rad * 2.0f;
    if (dia > r.Width) dia = r.Width;
    if (dia > r.Height) dia = r.Height;

    path.AddArc(r.X, r.Y, dia, dia, 180.0f, 90.0f);
    path.AddArc(r.X + r.Width - dia, r.Y, dia, dia, 270.0f, 90.0f);
    path.AddArc(r.X + r.Width - dia, r.Y + r.Height - dia, dia, dia, 0.0f, 90.0f);
    path.AddArc(r.X, r.Y + r.Height - dia, dia, dia, 90.0f, 90.0f);
    path.CloseFigure();
}

// Rounded rect with specific rounded corners for edge tabs
inline void AddTabPillToPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& r, DockSide side, float rad) {
    float dia = rad * 2.0f;
    float right = r.X + r.Width;
    float bottom = r.Y + r.Height;
    switch (side) {
        case DockSide::Right:
            // Rounded on Left (facing inside screen)
            path.AddArc(r.X, r.Y, dia, dia, 180.0f, 90.0f);
            path.AddLine(r.X + rad, r.Y, right, r.Y);
            path.AddLine(right, r.Y, right, bottom);
            path.AddLine(right, bottom, r.X + rad, bottom);
            path.AddArc(r.X, bottom - dia, dia, dia, 90.0f, 90.0f);
            path.CloseFigure();
            break;
        case DockSide::Left:
            // Rounded on Right (facing inside screen)
            path.AddLine(r.X, r.Y, right - rad, r.Y);
            path.AddArc(right - dia, r.Y, dia, dia, 270.0f, 90.0f);
            path.AddArc(right - dia, bottom - dia, dia, dia, 0.0f, 90.0f);
            path.AddLine(right - rad, bottom, r.X, bottom);
            path.AddLine(r.X, bottom, r.X, r.Y);
            path.CloseFigure();
            break;
        case DockSide::Top:
            // Rounded on Bottom (facing inside screen)
            path.AddLine(r.X, r.Y, right, r.Y);
            path.AddLine(right, r.Y, right, bottom - rad);
            path.AddArc(right - dia, bottom - dia, dia, dia, 0.0f, 90.0f);
            path.AddArc(r.X, bottom - dia, dia, dia, 90.0f, 90.0f);
            path.AddLine(r.X, bottom - rad, r.X, r.Y);
            path.CloseFigure();
            break;
        case DockSide::Bottom:
            // Rounded on Top (facing inside screen)
            path.AddArc(r.X, r.Y, dia, dia, 180.0f, 90.0f);
            path.AddArc(right - dia, r.Y, dia, dia, 270.0f, 90.0f);
            path.AddLine(right, r.Y + rad, right, bottom);
            path.AddLine(right, bottom, r.X, bottom);
            path.AddLine(r.X, bottom, r.X, r.Y + rad);
            path.CloseFigure();
            break;
    }
}

// Double buffer container
class DoubleBuffer {
public:
    HDC hdcMem = nullptr;
    HBITMAP hbmMem = nullptr;
    HBITMAP hbmOld = nullptr;
    int width = 0;
    int height = 0;

    void begin(HDC hdcTarget, int w, int h) {
        width = w;
        height = h;
        hdcMem = CreateCompatibleDC(hdcTarget);
        hbmMem = CreateCompatibleBitmap(hdcTarget, w, h);
        hbmOld = static_cast<HBITMAP>(SelectObject(hdcMem, hbmMem));
    }

    void end(HDC hdcTarget, int x = 0, int y = 0) {
        if (hdcMem && hdcTarget) {
            BitBlt(hdcTarget, x, y, width, height, hdcMem, 0, 0, SRCCOPY);
            SelectObject(hdcMem, hbmOld);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);
            hdcMem = nullptr;
            hbmMem = nullptr;
        }
    }

    ~DoubleBuffer() {
        if (hdcMem) {
            SelectObject(hdcMem, hbmOld);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);
        }
    }
};

} // namespace edgenotes
