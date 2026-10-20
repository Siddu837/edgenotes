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

// 8 pastel paper colors matching PeekyNotes exactly
inline const COLORREF PaperColors[8] = {
    RGB(167, 243, 208), // 0: Mint (#A7F3D0)
    RGB(186, 230, 253), // 1: Sky (#BAE6FD)
    RGB(221, 214, 254), // 2: Lavender (#DDD6FE)
    RGB(254, 240, 138), // 3: Yellow (#FEF08A)
    RGB(251, 207, 232), // 4: Rose (#FBCFE8)
    RGB(254, 215, 170), // 5: Peach (#FED7AA)
    RGB(153, 246, 228), // 6: Aqua (#99F6E4)
    RGB(226, 232, 240)  // 7: Slate / Paper (#E2E8F0)
};

inline const wchar_t* PaperColorNames[8] = {
    L"Mint", L"Sky", L"Lavender", L"Yellow", L"Rose", L"Peach", L"Aqua", L"Paper"
};

// Ink colors for text on pastel paper
inline const COLORREF PaperInk = RGB(46, 16, 101);         // Default dark ink
inline const COLORREF PaperMuted = RGB(100, 116, 139);     // Muted / metadata ink
inline const COLORREF PaperBorder = RGB(165, 185, 180);    // Subtle border

inline const COLORREF PaperInks[8] = {
    RGB(6, 78, 59),     // 0: Mint Ink (#064E3B)
    RGB(12, 74, 110),   // 1: Sky Ink (#0C4A6E)
    RGB(46, 16, 101),   // 2: Lavender Ink (#2E1065)
    RGB(113, 63, 18),   // 3: Yellow Ink (#713F12)
    RGB(131, 24, 67),   // 4: Rose Ink (#831843)
    RGB(124, 45, 18),   // 5: Peach Ink (#7C2D12)
    RGB(19, 78, 74),    // 6: Aqua Ink (#134E4A)
    RGB(30, 41, 59)     // 7: Slate Ink (#1E293B)
};

// Dark theme colors (for Deck & Manager)
inline const COLORREF NightSurface = RGB(22, 23, 25);      // Main background #161719
inline const COLORREF NightSidebar = RGB(18, 19, 21);      // Sidebar background #121315
inline const COLORREF NightPanel = RGB(30, 32, 36);        // Secondary panel / card #1E2024
inline const COLORREF NightPanelHover = RGB(37, 39, 44);   // Selected / hover card #25272C
inline const COLORREF NightBorder = RGB(43, 45, 51);       // Border #2B2D33
inline const COLORREF NightInk = RGB(241, 245, 249);       // Main light text #F1F5F9
inline const COLORREF NightMuted = RGB(148, 163, 184);     // Secondary text #94A3B8
inline const COLORREF NightAccent = RGB(79, 70, 229);      // Indigo Accent #4F46E5
inline const COLORREF NightAccentHover = RGB(99, 102, 241);// Light Indigo #6366F1
inline const COLORREF NightBanner = RGB(38, 28, 59);       // Trial banner purple #261C3B
inline const COLORREF NightSelected = RGB(79, 70, 229);    // Selected item highlight #4F46E5

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
