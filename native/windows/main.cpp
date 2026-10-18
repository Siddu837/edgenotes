#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "note-store.hpp"
#include "config.hpp"
#include "theme.hpp"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

using namespace edgenotes;

enum class AppState { Collapsed, Peek, Editing };
enum {
    CmdNewNote = 101,
    CmdAllNotes,
    CmdArchiveView,
    CmdDeleteNote,
    CmdUndoDelete,
    CmdCycleColor,
    CmdExportNote,
    CmdDockLeft,
    CmdDockRight,
    CmdDockTop,
    CmdDockBottom,
    CmdToggleDock,
    CmdExitApp,
    CmdBoldText,
    CmdItalicText,
    CmdHeading1,
    CmdHeading2,
    CmdHeading3,
    CmdNormalText,
    CmdBulletText,
    CmdChecklistText,
    CmdRestoreNote,
    CmdFilterAll,
    CmdFilterDeck,
    CmdFilterArchive,
    CmdPinEditor,
    CmdBackToDeck,
    CmdDockSettings,   // Open dock settings popup
    CmdThemeToggle,     // Toggle light/dark theme
    CmdUnpinDrawer,     // Unpin the drawer
    CmdAddToDesktop,    // Create desktop shortcut
    CmdPromptDesktopShortcut, // First run shortcut prompt
    CmdStrikeText,
    CmdNumberedList,
    CmdStartWithWindows,
    CmdInsertLink,
    CmdUndoText,
    CmdRedoText,
    CmdCodeBlock,
    CmdMoreMenu,
    CmdDuplicateNote,
    CmdMoveToTasks,
    CmdMoveToNotes,
    CmdMoveToThoughts,
    CmdMoveToArchive,
    CmdPortableMode,
    CmdSortOrder,
    CmdSortRecentlyEdited,
    CmdSortTitle,
    CmdImportNotes,
    CmdExportBackupJson,
    CmdFilterTasks,
    CmdFilterNotes,
    CmdFilterThoughts,
    CmdColorBase = 200  // 200..207 for 8 paper colors
};

constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_ANIM_STEP = WM_APP + 2;
UINT WM_ACTIVATE_INSTANCE = 0;
UINT WM_TASKBAR_CREATED = 0;

// Application instances & state
HINSTANCE appInstance = nullptr;
HWND dockHwnd = nullptr;
HWND peekHwnd = nullptr;
HWND editorHwnd = nullptr;
HWND managerHwnd = nullptr;
HWND titleBoxHwnd = nullptr;
HWND richEditHwnd = nullptr;
HWND statusBoxHwnd = nullptr;
HWND searchBoxHwnd = nullptr;

HFONT uiFont = nullptr;
HFONT uiBoldFont = nullptr;
HFONT titleFont = nullptr;
HFONT handFont = nullptr;
HFONT handPreviewFont = nullptr;
HFONT monoFont = nullptr;
HFONT tabRotatedFont = nullptr;

HICON appIcon = nullptr;
HMODULE richLibrary = nullptr;

Store store;
Config config;
AppState appState = AppState::Collapsed;
std::uint64_t activeId = 0;
std::uint64_t nextId = 1;
bool isDockVisible = true;
bool isLoadingEditor = false;
bool isSaveFailed = false;
bool isModalOpen = false;
bool isDraggingDock = false;
POINT dragStartCursor{};
float dragStartPosition = 0.5f;
int hoveredTabIndex = -1; // -1 none, 0..4 notes, 100 add, 101 theme, 102 settings
bool dockSettingsPopupOpen = false;
int hoveredPeekCard = -1;
int hoveredManagerRow = -1;
int hoveredToolbarBtn = 0;
int activeColorSwatch = 0;
int activeDeck = 0; // 0: TASKS, 1: NOTES, 2: THOUGHTS, 3: ARCHIVE
int managerSortMode = 0; // 0: Recently edited, 1: Title A-Z, 2: My order
int lastAutoFormatLine = -1;
std::wstring lastAutoFormatPrefix;

struct DeckDef {
    const wchar_t* name;
    const wchar_t* titleName;
    const wchar_t* symbol;
    COLORREF color;
};

const DeckDef DeckList[4] = {
    { L"TASKS",     L"Tasks",     L"\x2611", RGB(167, 243, 208) }, // \u2611 Mint
    { L"NOTES",     L"Notes",     L"\x2630", RGB(186, 230, 253) }, // \u2630 Sky
    { L"THOUGHTS",  L"Thoughts",  L"\x2726", RGB(254, 205, 211) }, // \u2726 Rose
    { L"ARCHIVE",   L"Archive",   L"\x25A3", RGB(233, 213, 255) }  // \u25A3 Lavender
};
std::string hotkeyCollisionNotice;

std::wstring storagePath;
std::wstring configPath;

// Manager state
int managerFilter = CmdFilterAll;
std::vector<std::uint64_t> managerRows;
std::uint64_t managerSelectedId = 0;

void LogDebug(const wchar_t* msg) {
    PWSTR localFolder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localFolder))) {
        std::wstring logPath = std::wstring(localFolder) + L"\\EdgeNotes\\debug.log";
        CoTaskMemFree(localFolder);
        FILE* fp = _wfopen(logPath.c_str(), L"a");
        if (fp) {
            fwprintf(fp, L"%ls\n", msg);
            fclose(fp);
        }
    }
}

// DPI helper
float GetDpiScale(HWND hwnd) {
    float result = 1.0f;
    if (hwnd) {
        HMODULE hUser = GetModuleHandleW(L"user32.dll");
        if (hUser) {
            typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
            auto pfn = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(hUser, "GetDpiForWindow"));
            if (pfn) {
                UINT dpi = pfn(hwnd);
                if (dpi > 0) result = static_cast<float>(dpi) / 96.0f;
            }
        }
    }
    if (result == 1.0f) {
        HMONITOR hMon = MonitorFromWindow(hwnd ? hwnd : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
        if (hMon) {
            HMODULE hShcore = LoadLibraryW(L"shcore.dll");
            if (hShcore) {
                typedef HRESULT(WINAPI * GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
                auto pfnMon = reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(hShcore, "GetDpiForMonitor"));
                if (pfnMon) {
                    UINT dpiX = 0, dpiY = 0;
                    if (SUCCEEDED(pfnMon(hMon, 0 /* MDT_EFFECTIVE_DPI */, &dpiX, &dpiY)) && dpiX > 0) {
                        result = static_cast<float>(dpiX) / 96.0f;
                    }
                }
            }
        }
    }
    return result;
}

int Scale(HWND hwnd, int val) {
    return static_cast<int>(std::round(val * GetDpiScale(hwnd)));
}

// UTF-8 string conversions
std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!count) return L"";
    std::wstring out(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), &out[0], count);
    return out;
}

std::string Utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], count, nullptr, nullptr);
    return out;
}

std::wstring GetWindowTextString(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return {};
    std::wstring out(len + 1, L'\0');
    GetWindowTextW(hwnd, &out[0], len + 1);
    out.resize(len);
    return out;
}

Note* GetActiveNote() {
    for (auto& n : store.notes) {
        if (n.id == activeId) return &n;
    }
    return nullptr;
}

Note* GetNoteById(std::uint64_t id) {
    for (auto& n : store.notes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

std::vector<Note*> GetDeckNotes() {
    std::vector<Note*> result;
    for (auto& n : store.notes) {
        if (!n.archived) {
            result.push_back(&n);
            if (result.size() >= 5) break;
        }
    }
    return result;
}

std::vector<Note*> GetNotesForActiveDeck() {
    std::vector<Note*> result;
    for (auto& n : store.notes) {
        if (activeDeck == 3) { // Archive
            if (n.archived) result.push_back(&n);
        } else {
            if (!n.archived) {
                int mapped = 0;
                if (n.color == 1) mapped = 1;       // Sky -> Notes
                else if (n.color == 4) mapped = 2;   // Rose -> Thoughts
                else mapped = 0;                      // Everything else -> Tasks
                if (mapped == activeDeck) result.push_back(&n);
            }
        }
    }
    if (result.empty() && activeDeck == 0) {
        for (auto& n : store.notes) {
            if (!n.archived) result.push_back(&n);
        }
    }
    return result;
}

bool AtomicWriteFile(const std::wstring& path, const std::string& data) {
    auto temp = path + L".tmp";
    HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && (written == data.size());
    if (ok) ok = FlushFileBuffers(h) != 0;
    CloseHandle(h);
    if (ok) ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
}

void PersistStore();
void UpdateStatusBox();
void SetAppState(AppState next, bool force = false);
void RefreshManagerList();
void SaveEditorContent();
void LoadActiveNoteIntoEditor();
void ExecuteCommand(int id);

void PersistStore() {
    try {
        isSaveFailed = !AtomicWriteFile(storagePath, edgenotes::encode(store.notes));
    } catch (...) {
        isSaveFailed = true;
    }
    UpdateStatusBox();
    if (dockHwnd) InvalidateRect(dockHwnd, nullptr, TRUE);
    if (peekHwnd && IsWindowVisible(peekHwnd)) InvalidateRect(peekHwnd, nullptr, TRUE);
    if (managerHwnd && IsWindowVisible(managerHwnd)) InvalidateRect(managerHwnd, nullptr, TRUE);
}

void UpdateStatusBox() {
    auto* n = GetActiveNote();
    if (!n || !statusBoxHwnd) return;
    bool hasUndo = store.deleted && (GetTickCount64() < store.deleted->expires);
    std::wstring caption = isSaveFailed ? L"Save failed - export your note" : L"\x25CF Saved locally";
    caption += L"   |   " + std::to_wstring(Wide(n->body).size()) + L" characters";
    if (hasUndo) {
        caption += L"   |   Undo deletion: Ctrl+Z";
    }
    if (!hotkeyCollisionNotice.empty()) {
        caption += L"   |   " + Wide(hotkeyCollisionNotice);
    }
    SetWindowTextW(statusBoxHwnd, caption.c_str());
}

// RTF Streaming for RichEdit
struct RtfStream {
    std::string bytes;
    std::size_t offset = 0;
};

DWORD CALLBACK RtfStreamOutCallback(DWORD_PTR cookie, LPBYTE buffer, LONG count, LONG* done) {
    auto* s = reinterpret_cast<RtfStream*>(cookie);
    s->bytes.append(reinterpret_cast<char*>(buffer), count);
    *done = count;
    return 0;
}

DWORD CALLBACK RtfStreamInCallback(DWORD_PTR cookie, LPBYTE buffer, LONG count, LONG* done) {
    auto* s = reinterpret_cast<RtfStream*>(cookie);
    auto size = std::min(static_cast<std::size_t>(count), s->bytes.size() - s->offset);
    std::memcpy(buffer, s->bytes.data() + s->offset, size);
    s->offset += size;
    *done = static_cast<LONG>(size);
    return 0;
}

void SaveEditorContent() {
    if (isLoadingEditor) return;
    auto* n = GetActiveNote();
    if (!n) return;
    n->title = Utf8(GetWindowTextString(titleBoxHwnd));
    n->body = Utf8(GetWindowTextString(richEditHwnd));
    RtfStream stream;
    EDITSTREAM es{};
    es.dwCookie = reinterpret_cast<DWORD_PTR>(&stream);
    es.pfnCallback = RtfStreamOutCallback;
    SendMessageW(richEditHwnd, EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&es));
    if (!es.dwError) {
        n->rtf = std::move(stream.bytes);
    }
    PersistStore();
}

void LoadActiveNoteIntoEditor() {
    auto* n = GetActiveNote();
    if (!n) return;
    isLoadingEditor = true;

    activeColorSwatch = n->color % 8;
    COLORREF paperColor = PaperColors[activeColorSwatch];

    SetWindowTextW(titleBoxHwnd, Wide(n->title).c_str());
    SendMessageW(richEditHwnd, EM_SETBKGNDCOLOR, 0, paperColor);

    if (n->rtf.empty()) {
        SetWindowTextW(richEditHwnd, Wide(n->body).c_str());
    } else {
        RtfStream stream{n->rtf};
        EDITSTREAM es{};
        es.dwCookie = reinterpret_cast<DWORD_PTR>(&stream);
        es.pfnCallback = RtfStreamInCallback;
        SendMessageW(richEditHwnd, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es));
    }

    CHARFORMAT2W fmt{};
    fmt.cbSize = sizeof(fmt);
    fmt.dwMask = CFM_COLOR;
    fmt.crTextColor = PaperInk;
    SendMessageW(richEditHwnd, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&fmt));
    SendMessageW(richEditHwnd, EM_EMPTYUNDOBUFFER, 0, 0);

    // Hide RichEdit scrollbar but allow mousewheel scrolling
    ShowScrollBar(richEditHwnd, SB_VERT, FALSE);

    isLoadingEditor = false;
    UpdateStatusBox();
    if (editorHwnd) InvalidateRect(editorHwnd, nullptr, TRUE);
}

RECT GetDockWorkArea() {
    HMONITOR hMon = MonitorFromWindow(dockHwnd ? dockHwnd : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(hMon, &info)) return info.rcWork;
    RECT r{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    return r;
}

void UpdateDockGeometry() {
    if (!dockHwnd) return;
    RECT work = GetDockWorkArea();

    bool isVertical = (config.side == DockSide::Left || config.side == DockSide::Right);

    int pillLen = Scale(dockHwnd, 64);   // Browser proportioned tabs
    int pillThick = Scale(dockHwnd, 42);  // Strip width
    int gripSize = Scale(dockHwnd, 24);
    int gap = Scale(dockHwnd, 4);
    int iconBtnSize = Scale(dockHwnd, 30);
    int iconGap = Scale(dockHwnd, 6);

    // Grip + 4 deck tabs + gap + [+] + [theme] + [pin] + [settings] + margins
    int totalLen = gripSize + gap + (4 * (pillLen + gap)) + Scale(dockHwnd, 10) + (4 * (iconBtnSize + iconGap)) + Scale(dockHwnd, 12);

    int x = 0, y = 0, w = 0, h = 0;
    if (isVertical) {
        w = pillThick;
        h = totalLen;
        int maxH = work.bottom - work.top - Scale(dockHwnd, 20);
        h = std::min(h, maxH);
        y = work.top + static_cast<int>((work.bottom - work.top - h) * config.position);
        y = std::max(static_cast<int>(work.top + Scale(dockHwnd, 5)), std::min(static_cast<int>(work.bottom - h - Scale(dockHwnd, 5)), y));
        x = (config.side == DockSide::Left) ? work.left : (work.right - w);
    } else {
        w = totalLen;
        h = pillThick;
        int maxW = work.right - work.left - Scale(dockHwnd, 20);
        w = std::min(w, maxW);
        x = work.left + static_cast<int>((work.right - work.left - w) * config.position);
        x = std::max(static_cast<int>(work.left + Scale(dockHwnd, 5)), std::min(static_cast<int>(work.right - w - Scale(dockHwnd, 5)), x));
        y = (config.side == DockSide::Top) ? work.top : (work.bottom - h);
    }

    SetWindowPos(dockHwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | (isDockVisible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    InvalidateRect(dockHwnd, nullptr, TRUE);
}

void UpdatePeekGeometry() {
    if (!peekHwnd || !dockHwnd) return;
    RECT dockRect{};
    GetWindowRect(dockHwnd, &dockRect);
    RECT work = GetDockWorkArea();

    int peekW = Scale(peekHwnd, 380);
    int peekH = Scale(peekHwnd, 520);
    peekH = std::min(peekH, static_cast<int>(work.bottom - work.top - Scale(peekHwnd, 40)));

    int x = 0, y = 0;
    int margin = Scale(peekHwnd, 10);

    if (config.side == DockSide::Right) {
        x = dockRect.left - peekW - margin;
        y = dockRect.top;
    } else if (config.side == DockSide::Left) {
        x = dockRect.right + margin;
        y = dockRect.top;
    } else if (config.side == DockSide::Top) {
        x = dockRect.left;
        y = dockRect.bottom + margin;
    } else { // Bottom
        x = dockRect.left;
        y = dockRect.top - peekH - margin;
    }

    x = std::max(static_cast<int>(work.left + margin), std::min(static_cast<int>(work.right - peekW - margin), x));
    y = std::max(static_cast<int>(work.top + margin), std::min(static_cast<int>(work.bottom - peekH - margin), y));

    SetWindowPos(peekHwnd, HWND_TOPMOST, x, y, peekW, peekH, SWP_NOACTIVATE);
}

void UpdateEditorGeometry() {
    if (!editorHwnd || !dockHwnd) return;
    RECT dockRect{};
    GetWindowRect(dockHwnd, &dockRect);
    RECT work = GetDockWorkArea();

    int editorW = Scale(editorHwnd, 580);
    int editorH = Scale(editorHwnd, 600);
    editorW = std::min(editorW, static_cast<int>(work.right - work.left - Scale(editorHwnd, 60)));
    editorH = std::min(editorH, static_cast<int>(work.bottom - work.top - Scale(editorHwnd, 60)));

    int x = 0, y = 0;
    int margin = Scale(editorHwnd, 15);

    if (config.side == DockSide::Right) {
        x = dockRect.left - editorW - margin;
        y = dockRect.top;
    } else if (config.side == DockSide::Left) {
        x = dockRect.right + margin;
        y = dockRect.top;
    } else if (config.side == DockSide::Top) {
        x = dockRect.left;
        y = dockRect.bottom + margin;
    } else { // Bottom
        x = dockRect.left;
        y = dockRect.top - editorH - margin;
    }

    x = std::max(static_cast<int>(work.left + margin), std::min(static_cast<int>(work.right - editorW - margin), x));
    y = std::max(static_cast<int>(work.top + margin), std::min(static_cast<int>(work.bottom - editorH - margin), y));

    SetWindowPos(editorHwnd, config.pinned ? HWND_TOPMOST : HWND_TOP, x, y, editorW, editorH, SWP_NOACTIVATE);
}

void SetAppState(AppState next, bool force) {
    if (appState == next && !force) return;
    appState = next;

    if (!isDockVisible) {
        isDockVisible = true;
        ShowWindow(dockHwnd, SW_SHOW);
    }
    UpdateDockGeometry();

    if (next == AppState::Collapsed) {
        ShowWindow(peekHwnd, SW_HIDE);
        ShowWindow(editorHwnd, SW_HIDE);
    } else if (next == AppState::Peek) {
        ShowWindow(editorHwnd, SW_HIDE);
        UpdatePeekGeometry();
        DWORD animFlags = AW_SLIDE | AW_ACTIVATE;
        if (config.side == DockSide::Left) animFlags |= AW_HOR_POSITIVE;
        else if (config.side == DockSide::Right) animFlags |= AW_HOR_NEGATIVE;
        else if (config.side == DockSide::Top) animFlags |= AW_VER_POSITIVE;
        else animFlags |= AW_VER_NEGATIVE;
        if (!AnimateWindow(peekHwnd, 120, animFlags)) {
            ShowWindow(peekHwnd, SW_SHOW);
        }
        SetForegroundWindow(peekHwnd);
        InvalidateRect(peekHwnd, nullptr, TRUE);
    } else if (next == AppState::Editing) {
        ShowWindow(peekHwnd, SW_HIDE);
        LoadActiveNoteIntoEditor();
        UpdateEditorGeometry();
        ShowWindow(editorHwnd, SW_SHOW);
        SetForegroundWindow(editorHwnd);
        InvalidateRect(editorHwnd, nullptr, TRUE);
    }
}

void ApplyFormat(DWORD mask, DWORD effect) {
    CHARFORMAT2W fmt{};
    fmt.cbSize = sizeof(fmt);
    fmt.dwMask = mask;
    SendMessageW(richEditHwnd, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt));
    fmt.dwEffects ^= effect;
    fmt.dwMask = mask;
    SendMessageW(richEditHwnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt));
    SetFocus(richEditHwnd);
    SaveEditorContent();
}

void ApplyFontSizeAndWeight(LONG yHeight, DWORD effects) {
    CHARFORMAT2W fmt{};
    fmt.cbSize = sizeof(fmt);
    fmt.dwMask = CFM_SIZE | CFM_BOLD;
    fmt.yHeight = yHeight;
    fmt.dwEffects = effects;
    SendMessageW(richEditHwnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt));
    SetFocus(richEditHwnd);
    SaveEditorContent();
}

void CreateDesktopShortcut() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    PWSTR desktopPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath))) {
        std::wstring linkPath = std::wstring(desktopPath) + L"\\EdgeNotes.lnk";
        CoTaskMemFree(desktopPath);

        IShellLinkW* psl = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&psl)))) {
            psl->SetPath(exePath);
            psl->SetDescription(L"EdgeNotes - Local notes on the edge of your screen");
            psl->SetIconLocation(exePath, 0);

            IPersistFile* ppf = nullptr;
            if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf)))) {
                ppf->Save(linkPath.c_str(), TRUE);
                ppf->Release();
            }
            psl->Release();
        }
    }
    CoUninitialize();
}

bool IsStartWithWindowsEnabled() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD size = 0;
        LONG res = RegQueryValueExW(hKey, L"EdgeNotes", nullptr, &type, nullptr, &size);
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    return false;
}

void ToggleStartWithWindows() {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        if (IsStartWithWindowsEnabled()) {
            RegDeleteValueW(hKey, L"EdgeNotes");
        } else {
            wchar_t exePath[MAX_PATH]{};
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            std::wstring quoted = L"\"" + std::wstring(exePath) + L"\"";
            RegSetValueExW(hKey, L"EdgeNotes", 0, REG_SZ, reinterpret_cast<const BYTE*>(quoted.c_str()), static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t)));
        }
        RegCloseKey(hKey);
    }
}

std::wstring GetExeDir() {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir = exePath;
    size_t lastSlash = exeDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) exeDir = exeDir.substr(0, lastSlash);
    return exeDir;
}

bool CheckIsPortableMode() {
    std::wstring exeDir = GetExeDir();
    return (GetFileAttributesW((exeDir + L"\\portable.flag").c_str()) != INVALID_FILE_ATTRIBUTES) ||
           (GetFileAttributesW((exeDir + L"\\notes.enotes").c_str()) != INVALID_FILE_ATTRIBUTES);
}

void TogglePortableMode() {
    std::wstring exeDir = GetExeDir();
    std::wstring flagFile = exeDir + L"\\portable.flag";
    if (GetFileAttributesW(flagFile.c_str()) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(flagFile.c_str());
        MessageBoxW(dockHwnd, L"Portable mode disabled. Notes will use %LocalAppData% on restart.", L"EdgeNotes", MB_ICONINFORMATION);
    } else {
        HANDLE h = CreateFileW(flagFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        AtomicWriteFile(exeDir + L"\\notes.enotes", edgenotes::encode(store.notes));
        MessageBoxW(dockHwnd, L"Portable mode enabled! Notes will be saved next to EdgeNotes.exe.", L"EdgeNotes", MB_ICONINFORMATION);
    }
}

bool IsForegroundAppFullscreen() {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == dockHwnd || fg == peekHwnd || fg == editorHwnd || fg == managerHwnd) return false;
    HMONITOR hMon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    if (GetMonitorInfoW(hMon, &mi)) {
        RECT fgRect;
        if (GetWindowRect(fg, &fgRect)) {
            if (fgRect.left <= mi.rcMonitor.left && fgRect.top <= mi.rcMonitor.top &&
                fgRect.right >= mi.rcMonitor.right && fgRect.bottom >= mi.rcMonitor.bottom) {
                wchar_t className[64]{};
                GetClassNameW(fg, className, 64);
                if (wcscmp(className, L"Progman") != 0 && wcscmp(className, L"WorkerW") != 0 && wcscmp(className, L"Shell_TrayWnd") != 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

std::string NotesToJson(const std::vector<Note>& notes) {
    std::string json = "[\n";
    for (size_t i = 0; i < notes.size(); ++i) {
        const auto& n = notes[i];
        json += "  {\n";
        json += "    \"id\": " + std::to_string(n.id) + ",\n";
        json += "    \"title\": \"";
        for (char c : n.title) {
            if (c == '"') json += "\\\"";
            else if (c == '\\') json += "\\\\";
            else if (c == '\n') json += "\\n";
            else if (c == '\r') json += "\\r";
            else if (c == '\t') json += "\\t";
            else json.push_back(c);
        }
        json += "\",\n";
        json += "    \"body\": \"";
        for (char c : n.body) {
            if (c == '"') json += "\\\"";
            else if (c == '\\') json += "\\\\";
            else if (c == '\n') json += "\\n";
            else if (c == '\r') json += "\\r";
            else if (c == '\t') json += "\\t";
            else json.push_back(c);
        }
        json += "\",\n";
        json += "    \"color\": " + std::to_string(n.color) + ",\n";
        json += "    \"archived\": " + std::string(n.archived ? "true" : "false") + "\n";
        json += (i + 1 == notes.size()) ? "  }\n" : "  },\n";
    }
    json += "]\n";
    return json;
}

void ExportBackupJson() {
    wchar_t path[MAX_PATH] = L"EdgeNotes-backup.json";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = managerHwnd ? managerHwnd : dockHwnd;
    ofn.lpstrFilter = L"JSON Backup (*.json)\0*.json\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"json";
    isModalOpen = true;
    if (GetSaveFileNameW(&ofn)) {
        std::string json = NotesToJson(store.notes);
        if (AtomicWriteFile(path, json)) {
            MessageBoxW(ofn.hwndOwner, L"Backup exported successfully!", L"EdgeNotes", MB_ICONINFORMATION);
        } else {
            MessageBoxW(ofn.hwndOwner, L"Failed to write backup file.", L"EdgeNotes", MB_ICONERROR);
        }
    }
    isModalOpen = false;
}

void ImportNotesFromFile() {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = managerHwnd ? managerHwnd : dockHwnd;
    ofn.lpstrFilter = L"Supported Files (*.md;*.txt;*.json)\0*.md;*.txt;*.json\0Markdown (*.md)\0*.md\0Plain Text (*.txt)\0*.txt\0JSON Backup (*.json)\0*.json\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    isModalOpen = true;
    if (GetOpenFileNameW(&ofn)) {
        HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER sz{};
            if (GetFileSizeEx(hFile, &sz) && sz.QuadPart > 0 && sz.QuadPart < 32 * 1024 * 1024) {
                std::string content(static_cast<size_t>(sz.QuadPart), '\0');
                DWORD read = 0;
                ReadFile(hFile, &content[0], static_cast<DWORD>(content.size()), &read, nullptr);
                CloseHandle(hFile);

                std::wstring wpath(path);
                bool isJson = (wpath.size() >= 5 && _wcsicmp(wpath.c_str() + wpath.size() - 5, L".json") == 0);
                if (isJson) {
                    size_t pos = 0;
                    int importedCount = 0;
                    while ((pos = content.find("\"title\":", pos)) != std::string::npos) {
                        pos += 8;
                        while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t' || content[pos] == '"')) pos++;
                        size_t endTitle = content.find("\"", pos);
                        if (endTitle == std::string::npos) break;
                        std::string title = content.substr(pos, endTitle - pos);
                        pos = endTitle + 1;

                        size_t bodyPos = content.find("\"body\":", pos);
                        if (bodyPos == std::string::npos) break;
                        bodyPos += 7;
                        while (bodyPos < content.size() && (content[bodyPos] == ' ' || content[bodyPos] == '\t' || content[bodyPos] == '"')) bodyPos++;
                        size_t endBody = content.find("\"", bodyPos);
                        if (endBody == std::string::npos) break;
                        std::string body = content.substr(bodyPos, endBody - bodyPos);
                        pos = endBody + 1;

                        Note n;
                        n.id = nextId++;
                        n.title = title.empty() ? "Imported note" : title;
                        n.body = body;
                        n.color = (activeDeck == 0 ? 0 : activeDeck == 1 ? 1 : activeDeck == 2 ? 4 : 2);
                        n.archived = false;
                        store.notes.push_back(n);
                        importedCount++;
                    }
                    PersistStore();
                    RefreshManagerList();
                    std::wstring msg = L"Imported " + std::to_wstring(importedCount) + L" notes!";
                    MessageBoxW(ofn.hwndOwner, msg.c_str(), L"EdgeNotes", MB_ICONINFORMATION);
                } else {
                    std::string title = "Imported Note";
                    std::string body = content;
                    size_t firstNewline = content.find_first_of("\r\n");
                    std::string firstLine = (firstNewline != std::string::npos) ? content.substr(0, firstNewline) : content;
                    if (firstLine.rfind("# ", 0) == 0) {
                        title = firstLine.substr(2);
                        body = (firstNewline != std::string::npos) ? content.substr(firstNewline + 1) : "";
                        while (!body.empty() && (body.front() == '\r' || body.front() == '\n')) body.erase(0, 1);
                    } else if (!firstLine.empty() && firstLine.size() <= 80) {
                        title = firstLine;
                    }
                    Note n;
                    n.id = nextId++;
                    n.title = title;
                    n.body = body;
                    n.color = (activeDeck == 0 ? 0 : activeDeck == 1 ? 1 : activeDeck == 2 ? 4 : 2);
                    n.archived = false;
                    store.notes.push_back(n);
                    PersistStore();
                    RefreshManagerList();
                    MessageBoxW(ofn.hwndOwner, L"Note imported successfully!", L"EdgeNotes", MB_ICONINFORMATION);
                }
            } else {
                CloseHandle(hFile);
            }
        }
    }
    isModalOpen = false;
}

void ShowEditorMoreMenu() {
    auto* n = GetActiveNote();
    if (!n) return;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, CmdDuplicateNote, L"Duplicate note");

    HMENU moveSub = CreatePopupMenu();
    AppendMenuW(moveSub, MF_STRING, CmdMoveToTasks, L"\x2611  Tasks");
    AppendMenuW(moveSub, MF_STRING, CmdMoveToNotes, L"\x2630  Notes");
    AppendMenuW(moveSub, MF_STRING, CmdMoveToThoughts, L"\x2726  Thoughts");
    AppendMenuW(moveSub, MF_STRING, CmdMoveToArchive, L"\x25A3  Archive");
    AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(moveSub), L"Move to deck");

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CmdExportNote, L"Export note\x2026");
    AppendMenuW(m, MF_STRING, CmdDeleteNote, L"Delete note\tCtrl+BkSp");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(editorHwnd);
    isModalOpen = true;
    int choice = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, editorHwnd, nullptr);
    DestroyMenu(m);
    isModalOpen = false;
    if (choice) ExecuteCommand(choice);
}

void ShowDockSettingsPopup() {
    dockSettingsPopupOpen = true;
    HMENU m = CreatePopupMenu();

    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"Make yourself at home");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(m, MF_STRING | (config.side == DockSide::Left ? MF_CHECKED : 0), CmdDockLeft, L"\x2190  Dock on left");
    AppendMenuW(m, MF_STRING | (config.side == DockSide::Right ? MF_CHECKED : 0), CmdDockRight, L"\x2192  Dock on right");
    AppendMenuW(m, MF_STRING | (config.side == DockSide::Top ? MF_CHECKED : 0), CmdDockTop, L"\x2191  Dock on top");
    AppendMenuW(m, MF_STRING | (config.side == DockSide::Bottom ? MF_CHECKED : 0), CmdDockBottom, L"\x2193  Dock on bottom");

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (config.pinned ? MF_CHECKED : 0), CmdUnpinDrawer, L"\x21F9  Pin dock open");
    AppendMenuW(m, MF_STRING | (IsStartWithWindowsEnabled() ? MF_CHECKED : 0), CmdStartWithWindows, L"Start with Windows");
    AppendMenuW(m, MF_STRING | (CheckIsPortableMode() ? MF_CHECKED : 0), CmdPortableMode, L"Portable mode");

    if (store.deleted && GetTickCount64() < store.deleted->expires) {
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, CmdUndoDelete, L"Undo deletion\tCtrl+Z");
    }

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CmdAddToDesktop, L"Add to Desktop");
    AppendMenuW(m, MF_STRING, CmdExitApp, L"Quit EdgeNotes");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(dockHwnd);
    int choice = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, dockHwnd, nullptr);
    DestroyMenu(m);
    dockSettingsPopupOpen = false;
    if (choice) ExecuteCommand(choice);
    PostMessageW(dockHwnd, WM_NULL, 0, 0);
}

void ShowTrayContextMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, CmdNewNote, L"New note\tCtrl+Alt+N");
    AppendMenuW(m, MF_STRING, CmdAllNotes, L"All notes\tCtrl+Alt+A");
    AppendMenuW(m, MF_STRING, CmdToggleDock, isDockVisible ? L"Hide dock\tCtrl+Alt+L" : L"Show dock\tCtrl+Alt+L");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    HMENU sub = CreatePopupMenu();
    AppendMenuW(sub, MF_STRING | (config.side == DockSide::Left ? MF_CHECKED : 0), CmdDockLeft, L"Left");
    AppendMenuW(sub, MF_STRING | (config.side == DockSide::Right ? MF_CHECKED : 0), CmdDockRight, L"Right");
    AppendMenuW(sub, MF_STRING | (config.side == DockSide::Top ? MF_CHECKED : 0), CmdDockTop, L"Top");
    AppendMenuW(sub, MF_STRING | (config.side == DockSide::Bottom ? MF_CHECKED : 0), CmdDockBottom, L"Bottom");
    AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), L"Dock position");

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (IsStartWithWindowsEnabled() ? MF_CHECKED : 0), CmdStartWithWindows, L"Start with Windows");
    AppendMenuW(m, MF_STRING | (CheckIsPortableMode() ? MF_CHECKED : 0), CmdPortableMode, L"Portable mode");

    if (store.deleted && GetTickCount64() < store.deleted->expires) {
        AppendMenuW(m, MF_STRING, CmdUndoDelete, L"Undo deletion\tCtrl+Z");
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CmdExitApp, L"Quit EdgeNotes");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(dockHwnd);
    int choice = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, dockHwnd, nullptr);
    DestroyMenu(m);
    if (choice) ExecuteCommand(choice);
    PostMessageW(dockHwnd, WM_NULL, 0, 0);
}

void ExecuteCommand(int id) {
    if (store.deleted && GetTickCount64() >= store.deleted->expires) {
        store.deleted.reset();
    }
    auto* n = GetActiveNote();

    switch (id) {
        case CmdNewNote: {
            Note note;
            note.id = nextId++;
            note.color = (activeDeck == 0 ? 0 : activeDeck == 1 ? 1 : activeDeck == 2 ? 4 : 2);
            note.archived = (activeDeck == 3);
            note.title = "Untitled note";
            activeId = note.id;
            store.notes.push_back(note);
            PersistStore();
            SetAppState(AppState::Editing);
            SetFocus(titleBoxHwnd);
            SendMessageW(titleBoxHwnd, EM_SETSEL, 0, -1);
            break;
        }
        case CmdAllNotes: {
            managerFilter = CmdFilterAll;
            RefreshManagerList();
            ShowWindow(managerHwnd, SW_SHOW);
            SetForegroundWindow(managerHwnd);
            SetFocus(searchBoxHwnd);
            break;
        }
        case CmdArchiveView: {
            managerFilter = CmdFilterArchive;
            RefreshManagerList();
            ShowWindow(managerHwnd, SW_SHOW);
            SetForegroundWindow(managerHwnd);
            SetFocus(searchBoxHwnd);
            break;
        }
        case CmdDeleteNote: {
            if (n) {
                store.erase(n->id, GetTickCount64());
                activeId = 0;
                PersistStore();
                RefreshManagerList();
                SetAppState(AppState::Collapsed);
            }
            break;
        }
        case CmdUndoDelete: {
            if (store.deleted && (GetTickCount64() < store.deleted->expires)) {
                auto idToRestore = store.deleted->note.id;
                if (store.undo(GetTickCount64())) {
                    activeId = idToRestore;
                    PersistStore();
                    RefreshManagerList();
                    SetAppState(AppState::Editing);
                }
            }
            break;
        }
        case CmdCycleColor: {
            if (n) {
                n->color = (n->color + 1) % 8;
                PersistStore();
                LoadActiveNoteIntoEditor();
            }
            break;
        }
        case CmdRestoreNote: {
            if (n) {
                n->archived = !n->archived;
                PersistStore();
                RefreshManagerList();
                if (n->archived && IsWindowVisible(editorHwnd)) {
                    SetAppState(AppState::Collapsed);
                }
            }
            break;
        }
        case CmdDockLeft: {
            config.side = DockSide::Left;
            config.save(configPath);
            UpdateDockGeometry();
            if (appState == AppState::Peek) UpdatePeekGeometry();
            if (appState == AppState::Editing) UpdateEditorGeometry();
            break;
        }
        case CmdDockRight: {
            config.side = DockSide::Right;
            config.save(configPath);
            UpdateDockGeometry();
            if (appState == AppState::Peek) UpdatePeekGeometry();
            if (appState == AppState::Editing) UpdateEditorGeometry();
            break;
        }
        case CmdDockTop: {
            config.side = DockSide::Top;
            config.save(configPath);
            UpdateDockGeometry();
            if (appState == AppState::Peek) UpdatePeekGeometry();
            if (appState == AppState::Editing) UpdateEditorGeometry();
            break;
        }
        case CmdDockBottom: {
            config.side = DockSide::Bottom;
            config.save(configPath);
            UpdateDockGeometry();
            if (appState == AppState::Peek) UpdatePeekGeometry();
            if (appState == AppState::Editing) UpdateEditorGeometry();
            break;
        }
        case CmdToggleDock: {
            isDockVisible = !isDockVisible;
            ShowWindow(dockHwnd, isDockVisible ? SW_SHOW : SW_HIDE);
            if (!isDockVisible) SetAppState(AppState::Collapsed);
            break;
        }
        case CmdPinEditor:
        case CmdUnpinDrawer: {
            config.pinned = !config.pinned;
            config.save(configPath);
            if (dockHwnd) InvalidateRect(dockHwnd, nullptr, TRUE);
            if (editorHwnd) {
                SetWindowPos(editorHwnd, config.pinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                InvalidateRect(editorHwnd, nullptr, TRUE);
            }
            break;
        }
        case CmdDockSettings: {
            ShowDockSettingsPopup();
            break;
        }
        case CmdThemeToggle: {
            config.themeMode = (config.themeMode + 1) % 3; // Cycle: Auto -> Light -> Dark
            config.save(configPath);
            InvalidateRect(dockHwnd, nullptr, TRUE);
            if (peekHwnd && IsWindowVisible(peekHwnd)) InvalidateRect(peekHwnd, nullptr, TRUE);
            if (editorHwnd && IsWindowVisible(editorHwnd)) InvalidateRect(editorHwnd, nullptr, TRUE);
            break;
        }
        case CmdAddToDesktop: {
            CreateDesktopShortcut();
            MessageBoxW(dockHwnd, L"EdgeNotes shortcut has been added to your Desktop!", L"EdgeNotes", MB_ICONINFORMATION);
            break;
        }
        case CmdPromptDesktopShortcut: {
            int res = MessageBoxW(dockHwnd,
                L"Welcome to EdgeNotes!\n\nWould you like to add an EdgeNotes shortcut to your Desktop for quick access?",
                L"EdgeNotes Setup",
                MB_YESNO | MB_ICONQUESTION | MB_TOPMOST);
            if (res == IDYES) {
                CreateDesktopShortcut();
            }
            break;
        }
        case CmdBackToDeck: {
            SetAppState(AppState::Peek);
            break;
        }
        case CmdBoldText: ApplyFormat(CFM_BOLD, CFE_BOLD); break;
        case CmdItalicText: ApplyFormat(CFM_ITALIC, CFE_ITALIC); break;
        case CmdStrikeText: ApplyFormat(CFM_STRIKEOUT, CFE_STRIKEOUT); break;
        case CmdHeading1: ApplyFontSizeAndWeight(480, CFE_BOLD); break; // 24pt
        case CmdHeading2: ApplyFontSizeAndWeight(380, CFE_BOLD); break; // 19pt
        case CmdHeading3: ApplyFontSizeAndWeight(320, CFE_BOLD); break; // 16pt
        case CmdNormalText: ApplyFontSizeAndWeight(260, 0); break;       // 13pt normal
        case CmdBulletText: {
            PARAFORMAT2 fmt{};
            fmt.cbSize = sizeof(fmt);
            fmt.dwMask = PFM_NUMBERING;
            fmt.wNumbering = PFN_BULLET;
            SendMessageW(richEditHwnd, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&fmt));
            SaveEditorContent();
            SetFocus(richEditHwnd);
            break;
        }
        case CmdChecklistText: {
            // Insert checkbox character or [ ]
            SendMessageW(richEditHwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\x25A2 "));
            SaveEditorContent();
            SetFocus(richEditHwnd);
            break;
        }
        case CmdNumberedList: {
            PARAFORMAT2 fmt{};
            fmt.cbSize = sizeof(fmt);
            fmt.dwMask = PFM_NUMBERING;
            fmt.wNumbering = 2; // PFN_ARABIC
            SendMessageW(richEditHwnd, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&fmt));
            SaveEditorContent();
            SetFocus(richEditHwnd);
            break;
        }
        case CmdInsertLink: {
            CHARRANGE sel{};
            SendMessageW(richEditHwnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
            if (sel.cpMin != sel.cpMax) {
                ApplyFormat(CFM_LINK | CFM_UNDERLINE, CFE_LINK | CFE_UNDERLINE);
            } else {
                SendMessageW(richEditHwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"https://"));
                SaveEditorContent();
            }
            SetFocus(richEditHwnd);
            break;
        }
        case CmdUndoText: {
            SendMessageW(richEditHwnd, EM_UNDO, 0, 0);
            SaveEditorContent();
            SetFocus(richEditHwnd);
            break;
        }
        case CmdRedoText: {
            SendMessageW(richEditHwnd, EM_REDO, 0, 0);
            SaveEditorContent();
            SetFocus(richEditHwnd);
            break;
        }
        case CmdCodeBlock: {
            CHARFORMAT2W fmt{};
            fmt.cbSize = sizeof(fmt);
            fmt.dwMask = CFM_FACE | CFM_SIZE;
            fmt.yHeight = 220; // 11pt
            lstrcpyW(fmt.szFaceName, L"Consolas");
            SendMessageW(richEditHwnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt));
            SendMessageW(richEditHwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"```\r\n// code\r\n```\r\n"));
            SaveEditorContent();
            SetFocus(richEditHwnd);
            break;
        }
        case CmdStartWithWindows: {
            ToggleStartWithWindows();
            break;
        }
        case CmdPortableMode: {
            TogglePortableMode();
            break;
        }
        case CmdMoreMenu: {
            ShowEditorMoreMenu();
            break;
        }
        case CmdDuplicateNote: {
            if (n) {
                Note dup = *n;
                dup.id = nextId++;
                dup.title += " (Copy)";
                store.notes.push_back(dup);
                activeId = dup.id;
                PersistStore();
                RefreshManagerList();
                LoadActiveNoteIntoEditor();
            }
            break;
        }
        case CmdMoveToTasks: {
            if (n) {
                n->color = 0;
                n->archived = false;
                PersistStore();
                RefreshManagerList();
                InvalidateRect(editorHwnd, nullptr, TRUE);
            }
            break;
        }
        case CmdMoveToNotes: {
            if (n) {
                n->color = 1;
                n->archived = false;
                PersistStore();
                RefreshManagerList();
                InvalidateRect(editorHwnd, nullptr, TRUE);
            }
            break;
        }
        case CmdMoveToThoughts: {
            if (n) {
                n->color = 4;
                n->archived = false;
                PersistStore();
                RefreshManagerList();
                InvalidateRect(editorHwnd, nullptr, TRUE);
            }
            break;
        }
        case CmdMoveToArchive: {
            if (n) {
                n->archived = true;
                PersistStore();
                RefreshManagerList();
                SetAppState(AppState::Collapsed);
            }
            break;
        }
        case CmdImportNotes: {
            ImportNotesFromFile();
            break;
        }
        case CmdExportBackupJson: {
            ExportBackupJson();
            break;
        }
        case CmdSortRecentlyEdited: {
            managerSortMode = 0;
            RefreshManagerList();
            break;
        }
        case CmdSortTitle: {
            managerSortMode = 1;
            RefreshManagerList();
            break;
        }
        case CmdSortOrder: {
            managerSortMode = 2;
            RefreshManagerList();
            break;
        }
        case CmdExportNote: {
            if (n) {
                wchar_t path[MAX_PATH] = L"EdgeNotes-note.md";
                OPENFILENAMEW ofn{};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = editorHwnd;
                ofn.lpstrFilter = L"Markdown (*.md)\0*.md\0Plain text (*.txt)\0*.txt\0Rich text (*.rtf)\0*.rtf\0\0";
                ofn.lpstrFile = path;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_OVERWRITEPROMPT;
                ofn.lpstrDefExt = L"md";
                isModalOpen = true;
                if (GetSaveFileNameW(&ofn)) {
                    bool isRtf = (ofn.nFilterIndex == 3);
                    bool isMd = (ofn.nFilterIndex == 1);
                    std::string content;
                    if (isRtf) {
                        content = n->rtf;
                    } else if (isMd) {
                        content = "# " + n->title + "\n\n" + n->body;
                    } else {
                        content = n->title + "\r\n\r\n" + n->body;
                    }
                    if (!AtomicWriteFile(path, content)) {
                        MessageBoxW(editorHwnd, L"Could not export this note.", L"EdgeNotes", MB_ICONERROR);
                    }
                }
                isModalOpen = false;
            }
            break;
        }
        case CmdExitApp: {
            NOTIFYICONDATAW nidDel{};
            nidDel.cbSize = sizeof(nidDel);
            nidDel.hWnd = dockHwnd;
            nidDel.uID = 1;
            Shell_NotifyIconW(NIM_DELETE, &nidDel);
            PostQuitMessage(0);
            break;
        }
        default: {
            if (id >= CmdColorBase && id < CmdColorBase + 8) {
                int c = id - CmdColorBase;
                if (n) {
                    n->color = c;
                    PersistStore();
                    LoadActiveNoteIntoEditor();
                }
            }
            break;
        }
    }
}

// Subclass procedure for titleBox and richEdit controls
LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN) {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (wp == VK_ESCAPE) {
            SetAppState(AppState::Collapsed);
            return 0;
        }
        if (ctrl && wp == VK_BACK) {
            ExecuteCommand(CmdDeleteNote);
            return 0;
        }
        if (ctrl && wp == VK_OEM_PERIOD) {
            ExecuteCommand(CmdCycleColor);
            return 0;
        }
        if (ctrl && !shift && wp == 'Z' && store.deleted) {
            ExecuteCommand(CmdUndoDelete);
            return 0;
        }
        if (ctrl && !shift && wp == 'B') {
            ExecuteCommand(CmdBoldText);
            return 0;
        }
        if (ctrl && !shift && wp == 'I') {
            ExecuteCommand(CmdItalicText);
            return 0;
        }
        if (ctrl && !shift && wp == 'K') {
            ExecuteCommand(CmdInsertLink);
            return 0;
        }
        if (ctrl && shift && (wp == '8')) {
            ExecuteCommand(CmdBulletText);
            return 0;
        }
        if (ctrl && shift && (wp == 'L')) {
            ExecuteCommand(CmdChecklistText);
            return 0;
        }
        if (ctrl && shift && (wp == 'X' || wp == 'S')) {
            ExecuteCommand(CmdStrikeText);
            return 0;
        }
        if (ctrl && (wp == 'Y' || (shift && wp == 'Z'))) {
            ExecuteCommand(CmdRedoText);
            return 0;
        }
        if (wp == VK_BACK && !lastAutoFormatPrefix.empty() && hwnd == richEditHwnd) {
            CHARRANGE sel{};
            SendMessageW(hwnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
            LONG currentLine = static_cast<LONG>(SendMessageW(hwnd, EM_LINEFROMCHAR, sel.cpMin, 0));
            LONG start = static_cast<LONG>(SendMessageW(hwnd, EM_LINEINDEX, currentLine, 0));
            if (currentLine == lastAutoFormatLine && sel.cpMin == start) {
                ExecuteCommand(CmdNormalText);
                SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>((lastAutoFormatPrefix + L" ").c_str()));
                lastAutoFormatPrefix.clear();
                lastAutoFormatLine = -1;
                return 0;
            }
        }
        if (hwnd == titleBoxHwnd && (wp == VK_RETURN || wp == VK_DOWN)) {
            SetFocus(richEditHwnd);
            return 0;
        }
    }

    // Enter in RichEdit after a heading returns formatting to normal body text
    if (msg == WM_CHAR && wp == VK_RETURN && hwnd == richEditHwnd) {
        lastAutoFormatPrefix.clear();
        lastAutoFormatLine = -1;
        auto res = DefSubclassProc(hwnd, msg, wp, lp);
        CHARFORMAT2W fmt{};
        fmt.cbSize = sizeof(fmt);
        fmt.dwMask = CFM_SIZE | CFM_BOLD;
        fmt.yHeight = 260; // 13pt
        fmt.dwEffects = 0;
        SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt));
        return res;
    }

    // Auto-formatting triggers: "# ", "## ", "### ", "- ", "* ", "[] ", "1. "
    if (msg == WM_CHAR && wp == ' ' && hwnd == richEditHwnd) {
        CHARRANGE selection{};
        SendMessageW(hwnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
        LONG line = static_cast<LONG>(SendMessageW(hwnd, EM_LINEFROMCHAR, selection.cpMin, 0));
        LONG start = static_cast<LONG>(SendMessageW(hwnd, EM_LINEINDEX, line, 0));
        LONG len = selection.cpMin - start;
        if (selection.cpMin == selection.cpMax && len > 0 && len <= 3) {
            wchar_t buf[8]{};
            TEXTRANGEW range{{start, selection.cpMin}, buf};
            SendMessageW(hwnd, EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&range));
            std::wstring trigger(buf);
            if (trigger == L"#" || trigger == L"##" || trigger == L"###" || trigger == L"-" || trigger == L"*" || trigger == L"[]" || trigger == L"1.") {
                lastAutoFormatPrefix = trigger;
                lastAutoFormatLine = line;
                isLoadingEditor = true;
                CHARRANGE removeRange{start, selection.cpMin};
                SendMessageW(hwnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&removeRange));
                SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
                isLoadingEditor = false;

                if (trigger == L"-" || trigger == L"*") {
                    ExecuteCommand(CmdBulletText);
                } else if (trigger == L"[]") {
                    ExecuteCommand(CmdChecklistText);
                } else if (trigger == L"1.") {
                    ExecuteCommand(CmdNumberedList);
                } else {
                    CHARFORMAT2W fmt{};
                    fmt.cbSize = sizeof(fmt);
                    fmt.dwMask = CFM_SIZE | CFM_BOLD;
                    fmt.dwEffects = CFE_BOLD;
                    fmt.yHeight = (trigger.size() == 1) ? 480 : (trigger.size() == 2) ? 380 : 320;
                    SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt));
                    SaveEditorContent();
                }
                return 0;
            }
        }
    }

    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// 1. DOCK WINDOW PAINT & LOGIC
// ---------------------------------------------------------------------------
void PaintDock(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);

    DoubleBuffer buffer;
    buffer.begin(hdc, client.right, client.bottom);
    Gdiplus::Graphics g(buffer.hdcMem);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // Fill with key color for transparency
    g.Clear(Gdiplus::Color(255, 12, 14, 18));

    bool isVertical = (config.side == DockSide::Left || config.side == DockSide::Right);

    int gripSize = Scale(hwnd, 24);
    int pillLen = Scale(hwnd, 64);
    int pillThick = Scale(hwnd, 42);
    int gap = Scale(hwnd, 4);
    int iconBtnSize = Scale(hwnd, 30);
    int iconGap = Scale(hwnd, 6);

    // 1. Drag Grip at the top/start (3x3 circular dots)
    Gdiplus::SolidBrush gripBrush(Gdiplus::Color(210, 255, 255, 255));
    int gripDotsX = isVertical ? (client.right / 2 - Scale(hwnd, 5)) : Scale(hwnd, 10);
    int gripDotsY = isVertical ? Scale(hwnd, 8) : (client.bottom / 2 - Scale(hwnd, 5));
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            g.FillEllipse(&gripBrush, gripDotsX + c * Scale(hwnd, 5), gripDotsY + r * Scale(hwnd, 5), Scale(hwnd, 2), Scale(hwnd, 2));
        }
    }

    int currentPos = gripSize + gap;

    // 2. The 5 Deck Tabs: WORK, PERSONAL, SCRATCHPAD, IDEAS, ARCHIVE
    for (int i = 0; i < 4; ++i) {
        bool isHovered = (hoveredTabIndex == i);
        bool isActive = (activeDeck == i && (appState == AppState::Peek || appState == AppState::Editing));

        int hoverOffset = isHovered ? Scale(hwnd, 8) : (isActive ? Scale(hwnd, 4) : 0);

        Gdiplus::RectF tabRect;
        if (isVertical) {
            float x = (config.side == DockSide::Right) ? static_cast<float>(-hoverOffset) : 0.0f;
            tabRect = Gdiplus::RectF(x, static_cast<float>(currentPos), static_cast<float>(pillThick + hoverOffset), static_cast<float>(pillLen));
        } else {
            float y = (config.side == DockSide::Bottom) ? static_cast<float>(-hoverOffset) : 0.0f;
            tabRect = Gdiplus::RectF(static_cast<float>(currentPos), y, static_cast<float>(pillLen), static_cast<float>(pillThick + hoverOffset));
        }

        // Pill shape with rounded corners on inner side
        Gdiplus::GraphicsPath path;
        AddTabPillToPath(path, tabRect, config.side, static_cast<float>(Scale(hwnd, 8)));

        COLORREF colorVal = DeckList[i].color;
        Gdiplus::SolidBrush tabBrush(ToGdiColor(colorVal));
        g.FillPath(&tabBrush, &path);

        Gdiplus::Pen borderPen(Gdiplus::Color(45, 0, 0, 0), 1.0f);
        g.DrawPath(&borderPen, &path);

        // Deck label: uppercase bold
        std::wstring titleText = std::wstring(DeckList[i].symbol) + L" " + DeckList[i].name;
        Gdiplus::SolidBrush textBrush(ToGdiColor(PaperInk));
        Gdiplus::Font font(L"Segoe UI", static_cast<float>(Scale(hwnd, 11)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

        if (isVertical) {
            Gdiplus::GraphicsState gState = g.Save();
            float cx = tabRect.X + tabRect.Width / 2.0f;
            float cy = tabRect.Y + tabRect.Height / 2.0f;
            g.TranslateTransform(cx, cy);
            g.RotateTransform(config.side == DockSide::Right ? 270.0f : 90.0f);

            Gdiplus::RectF textBound(-tabRect.Height / 2.0f, -tabRect.Width / 2.0f, tabRect.Height, tabRect.Width);
            g.DrawString(titleText.c_str(), -1, &font, textBound, &sf, &textBrush);
            g.Restore(gState);
        } else {
            g.DrawString(titleText.c_str(), -1, &font, tabRect, &sf, &textBrush);
        }

        currentPos += pillLen + gap;
    }

    currentPos += Scale(hwnd, 10);

    // 3. Three circular icon buttons below tabs: [+] [☀/🌙] [🎛]
    auto drawCircleButton = [&](int btnIndex, auto drawIcon) {
        bool isHovered = (hoveredTabIndex == btnIndex);
        Gdiplus::RectF btnRect;
        if (isVertical) {
            float bx = (static_cast<float>(client.right) - static_cast<float>(iconBtnSize)) / 2.0f;
            btnRect = Gdiplus::RectF(bx, static_cast<float>(currentPos), static_cast<float>(iconBtnSize), static_cast<float>(iconBtnSize));
        } else {
            float by = (static_cast<float>(client.bottom) - static_cast<float>(iconBtnSize)) / 2.0f;
            btnRect = Gdiplus::RectF(static_cast<float>(currentPos), by, static_cast<float>(iconBtnSize), static_cast<float>(iconBtnSize));
        }

        Gdiplus::GraphicsPath btnPath;
        btnPath.AddEllipse(btnRect);

        Gdiplus::Color bgColor = isHovered ? Gdiplus::Color(255, 45, 48, 58) : Gdiplus::Color(255, 24, 26, 32);
        Gdiplus::SolidBrush bgBrush(bgColor);
        g.FillPath(&bgBrush, &btnPath);

        Gdiplus::Pen borderPen(Gdiplus::Color(60, 255, 255, 255), 1.0f);
        g.DrawPath(&borderPen, &btnPath);

        drawIcon(btnRect, isHovered);
        currentPos += iconBtnSize + iconGap;
    };

    // Button 100: [+] (New note)
    drawCircleButton(100, [&](Gdiplus::RectF r, bool) {
        Gdiplus::Font plusFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 18)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush plusText(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::StringFormat csf;
        csf.SetAlignment(Gdiplus::StringAlignmentCenter);
        csf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(L"+", -1, &plusFont, r, &csf, &plusText);
    });

    // Button 101: [☀ / 🌙] (Theme toggle)
    drawCircleButton(101, [&](Gdiplus::RectF r, bool) {
        Gdiplus::Font themeFont(L"Segoe UI Symbol", static_cast<float>(Scale(hwnd, 13)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush themeText(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::StringFormat csf;
        csf.SetAlignment(Gdiplus::StringAlignmentCenter);
        csf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        const wchar_t* symbol = (config.themeMode == 1) ? L"\x2600" : L"\x263E"; // ☀ or ☾
        g.DrawString(symbol, -1, &themeFont, r, &csf, &themeText);
    });

    // Button 102: [📌] (Pin button - keeps dock & editor open)
    drawCircleButton(102, [&](Gdiplus::RectF r, bool) {
        Gdiplus::Font pinFont(L"Segoe UI Symbol", static_cast<float>(Scale(hwnd, 13)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Color textColor = config.pinned ? Gdiplus::Color(255, 167, 243, 208) : Gdiplus::Color(255, 255, 255, 255);
        Gdiplus::SolidBrush pinText(textColor);
        Gdiplus::StringFormat csf;
        csf.SetAlignment(Gdiplus::StringAlignmentCenter);
        csf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        const wchar_t* symbol = config.pinned ? L"\x2297" : L"\x2299"; // ⊗ (pinned) or ⊙ (unpinned)
        g.DrawString(symbol, -1, &pinFont, r, &csf, &pinText);
    });

    // Button 103: [🎛] (Dock settings / Sliders)
    drawCircleButton(103, [&](Gdiplus::RectF r, bool) {
        float cx = r.X + r.Width / 2.0f;
        float cy = r.Y + r.Height / 2.0f;
        float lineW = static_cast<float>(Scale(hwnd, 14));
        float x1 = cx - lineW / 2.0f;
        float x2 = cx + lineW / 2.0f;

        Gdiplus::Pen pen(Gdiplus::Color(255, 255, 255, 255), 1.5f);
        Gdiplus::SolidBrush knobBrush(Gdiplus::Color(255, 255, 255, 255));

        // Line 1 (top)
        float y1 = cy - static_cast<float>(Scale(hwnd, 4));
        g.DrawLine(&pen, x1, y1, x2, y1);
        g.FillEllipse(&knobBrush, x1 + static_cast<float>(Scale(hwnd, 3)), y1 - static_cast<float>(Scale(hwnd, 2)), static_cast<float>(Scale(hwnd, 4)), static_cast<float>(Scale(hwnd, 4)));

        // Line 2 (bottom)
        float y2 = cy + static_cast<float>(Scale(hwnd, 4));
        g.DrawLine(&pen, x1, y2, x2, y2);
        g.FillEllipse(&knobBrush, x2 - static_cast<float>(Scale(hwnd, 7)), y2 - static_cast<float>(Scale(hwnd, 2)), static_cast<float>(Scale(hwnd, 4)), static_cast<float>(Scale(hwnd, 4)));
    });

    buffer.end(hdc);
    EndPaint(hwnd, &ps);
}

int HitTestDock(HWND hwnd, int x, int y) {
    bool isVertical = (config.side == DockSide::Left || config.side == DockSide::Right);

    int gripSize = Scale(hwnd, 24);
    int pillLen = Scale(hwnd, 64);
    int gap = Scale(hwnd, 4);
    int iconBtnSize = Scale(hwnd, 30);
    int iconGap = Scale(hwnd, 6);

    int pos = isVertical ? y : x;

    if (pos < gripSize) return -2; // Grip

    int current = gripSize + gap;
    for (int i = 0; i < 4; ++i) {
        if (pos >= current && pos < current + pillLen) {
            return i; // Deck tab index 0..3
        }
        current += pillLen + gap;
    }

    current += Scale(hwnd, 10);

    if (pos >= current && pos < current + iconBtnSize) return 100; // '+' button
    current += iconBtnSize + iconGap;

    if (pos >= current && pos < current + iconBtnSize) return 101; // Theme button
    current += iconBtnSize + iconGap;

    if (pos >= current && pos < current + iconBtnSize) return 102; // Pin button
    current += iconBtnSize + iconGap;

    if (pos >= current && pos < current + iconBtnSize) return 103; // Settings button

    return -1;
}

LRESULT CALLBACK DockWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            PaintDock(hwnd);
            return 0;

        case WM_COMMAND:
            ExecuteCommand(LOWORD(wp));
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_SETCURSOR: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            int hit = HitTestDock(hwnd, pt.x, pt.y);
            if (hit == -2 || (hit >= 0 && hit < 4) || (hit >= 100 && hit <= 103)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);

            if (isDraggingDock) {
                POINT pt;
                GetCursorPos(&pt);
                RECT work = GetDockWorkArea();

                int dLeft = pt.x - work.left;
                int dRight = work.right - pt.x;
                int dTop = pt.y - work.top;
                int dBottom = work.bottom - pt.y;

                int minD = std::min({dLeft, dRight, dTop, dBottom});
                DockSide newSide = config.side;
                if (minD == dLeft) newSide = DockSide::Left;
                else if (minD == dRight) newSide = DockSide::Right;
                else if (minD == dTop) newSide = DockSide::Top;
                else newSide = DockSide::Bottom;

                if (newSide != config.side) {
                    config.side = newSide;
                }

                if (config.side == DockSide::Left || config.side == DockSide::Right) {
                    float rel = static_cast<float>(pt.y - work.top) / static_cast<float>(work.bottom - work.top);
                    config.position = Clamp(rel, 0.05f, 0.95f);
                } else {
                    float rel = static_cast<float>(pt.x - work.left) / static_cast<float>(work.right - work.left);
                    config.position = Clamp(rel, 0.05f, 0.95f);
                }

                UpdateDockGeometry();
                if (appState == AppState::Peek) UpdatePeekGeometry();
                if (appState == AppState::Editing) UpdateEditorGeometry();
                return 0;
            }

            int hit = HitTestDock(hwnd, x, y);
            if (hit != hoveredTabIndex) {
                hoveredTabIndex = hit;
                InvalidateRect(hwnd, nullptr, FALSE);
            }

            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE: {
            if (!isDraggingDock) {
                hoveredTabIndex = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            int hit = HitTestDock(hwnd, x, y);

            if (hit == -2) {
                // Grip dragging
                isDraggingDock = true;
                SetCapture(hwnd);
                GetCursorPos(&dragStartCursor);
                dragStartPosition = config.position;
                return 0;
            }

            if (hit >= 0 && hit < 4) {
                activeDeck = hit;
                if (appState == AppState::Collapsed) {
                    SetAppState(AppState::Peek);
                } else if (appState == AppState::Peek) {
                    InvalidateRect(peekHwnd, nullptr, TRUE);
                    InvalidateRect(dockHwnd, nullptr, TRUE);
                } else if (appState == AppState::Editing) {
                    auto deckNotes = GetNotesForActiveDeck();
                    if (!deckNotes.empty()) {
                        activeId = deckNotes[0]->id;
                        LoadActiveNoteIntoEditor();
                    } else {
                        ExecuteCommand(CmdNewNote);
                    }
                    InvalidateRect(dockHwnd, nullptr, TRUE);
                }
                return 0;
            }

            if (hit == 100) {
                ExecuteCommand(CmdNewNote);
                return 0;
            }

            if (hit == 101) {
                ExecuteCommand(CmdThemeToggle);
                return 0;
            }

            if (hit == 102) {
                ExecuteCommand(CmdPinEditor);
                return 0;
            }

            if (hit == 103) {
                ExecuteCommand(CmdDockSettings);
                return 0;
            }

            // Clicked background of dock
            if (appState == AppState::Collapsed) {
                SetAppState(AppState::Peek);
            } else {
                SetAppState(AppState::Collapsed);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (isDraggingDock) {
                isDraggingDock = false;
                ReleaseCapture();
                config.save(configPath);
            }
            return 0;
        }

        case WM_RBUTTONUP:
            ShowTrayContextMenu();
            return 0;

        case WM_DPICHANGED: {
            UpdateDockGeometry();
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// 2. PEEK DECK WINDOW PAINT & LOGIC
// ---------------------------------------------------------------------------
void PaintPeek(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);

    DoubleBuffer buffer;
    buffer.begin(hdc, client.right, client.bottom);
    Gdiplus::Graphics g(buffer.hdcMem);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // Dark backdrop with rounded border
    Gdiplus::RectF fullRect(0.0f, 0.0f, static_cast<float>(client.right), static_cast<float>(client.bottom));
    Gdiplus::GraphicsPath bgPath;
    AddRoundedRectToPath(bgPath, fullRect, static_cast<float>(Scale(hwnd, 10)));
    Gdiplus::SolidBrush bgBrush(ToGdiColor(NightSurface));
    g.FillPath(&bgBrush, &bgPath);
    Gdiplus::Pen borderPen(ToGdiColor(NightBorder), 1.0f);
    g.DrawPath(&borderPen, &bgPath);

    // Header: Dot, Deck Name, count, close (»)
    int pad = Scale(hwnd, 16);
    int topH = Scale(hwnd, 44);

    Gdiplus::SolidBrush dotBrush(ToGdiColor(DeckList[activeDeck].color));
    g.FillEllipse(&dotBrush, pad, Scale(hwnd, 18), Scale(hwnd, 8), Scale(hwnd, 8));

    Gdiplus::Font headerFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 14)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush textBrush(ToGdiColor(NightInk));
    std::wstring deckTitle = DeckList[activeDeck].titleName;
    g.DrawString(deckTitle.c_str(), -1, &headerFont, Gdiplus::PointF(static_cast<float>(pad + Scale(hwnd, 14)), static_cast<float>(Scale(hwnd, 14))), &textBrush);

    auto notes = GetNotesForActiveDeck();
    std::wstring countStr = std::to_wstring(notes.size()) + (notes.size() == 1 ? L" thought" : L" thoughts");
    Gdiplus::Font countFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 11)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush mutedBrush(ToGdiColor(NightMuted));
    float countX = pad + Scale(hwnd, 20) + (static_cast<float>(deckTitle.length()) * Scale(hwnd, 10));
    g.DrawString(countStr.c_str(), -1, &countFont, Gdiplus::PointF(countX, static_cast<float>(Scale(hwnd, 16))), &mutedBrush);

    // Close button (» matching browser peek deck)
    Gdiplus::Font closeFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 16)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::RectF closeRect(static_cast<float>(client.right - pad - Scale(hwnd, 24)), static_cast<float>(Scale(hwnd, 12)), static_cast<float>(Scale(hwnd, 24)), static_cast<float>(Scale(hwnd, 20)));
    Gdiplus::StringFormat centerSf;
    centerSf.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    g.DrawString(L"\x00BB", -1, &closeFont, closeRect, &centerSf, &mutedBrush);

    // Cards area: first 5 non-archived notes in this deck
    int cardY = topH + Scale(hwnd, 4);
    int cardH = Scale(hwnd, 82);
    int cardGap = Scale(hwnd, 8);
    int cardW = client.right - pad * 2;

    if (notes.empty()) {
        // "A fresh page" empty card
        Gdiplus::RectF emptyRect(static_cast<float>(pad), static_cast<float>(cardY), static_cast<float>(cardW), static_cast<float>(cardH));
        Gdiplus::GraphicsPath emptyPath;
        AddRoundedRectToPath(emptyPath, emptyRect, static_cast<float>(Scale(hwnd, 7)));
        Gdiplus::SolidBrush emptyBrush(ToGdiColor(NightPanel));
        g.FillPath(&emptyBrush, &emptyPath);
        Gdiplus::Pen emptyBorder(ToGdiColor(NightBorder), 1.0f);
        g.DrawPath(&emptyBorder, &emptyPath);

        Gdiplus::Font emptyFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 13)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        g.DrawString(L"+ A fresh page", -1, &emptyFont, emptyRect, &centerSf, &textBrush);
    } else {
        for (std::size_t i = 0; i < notes.size(); ++i) {
            const auto* n = notes[i];
            bool isHovered = (hoveredPeekCard == static_cast<int>(i));

            Gdiplus::RectF cardRect(static_cast<float>(pad), static_cast<float>(cardY), static_cast<float>(cardW), static_cast<float>(cardH));
            Gdiplus::GraphicsPath cardPath;
            AddRoundedRectToPath(cardPath, cardRect, static_cast<float>(Scale(hwnd, 7)));

            COLORREF paperColor = PaperColors[n->color % 8];
            Gdiplus::SolidBrush cardBrush(ToGdiColor(paperColor));
            g.FillPath(&cardBrush, &cardPath);

            if (isHovered) {
                Gdiplus::Pen hoverPen(ToGdiColor(NightInk, 180), 2.0f);
                g.DrawPath(&hoverPen, &cardPath);
            } else {
                Gdiplus::Pen cardPen(ToGdiColor(PaperBorder, 80), 1.0f);
                g.DrawPath(&cardPen, &cardPath);
            }

            // Note title & arrow icon
            std::wstring titleText = Wide(n->title.empty() ? "Untitled note" : n->title);
            Gdiplus::Font titleCardFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 12)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush inkBrush(ToGdiColor(PaperInk));
            g.DrawString(titleText.c_str(), -1, &titleCardFont, Gdiplus::PointF(cardRect.X + Scale(hwnd, 12), cardRect.Y + Scale(hwnd, 8)), &inkBrush);

            Gdiplus::Font arrowFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 11)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush arrowBrush(ToGdiColor(PaperMuted));
            g.DrawString(L"\x2197", -1, &arrowFont, Gdiplus::PointF(cardRect.X + cardRect.Width - Scale(hwnd, 20), cardRect.Y + Scale(hwnd, 8)), &arrowBrush);

            // Note snippet in Segoe Print handwriting
            std::wstring bodySnippet = Wide(n->body.substr(0, 140));
            Gdiplus::Font bodyFont(L"Segoe Print", static_cast<float>(Scale(hwnd, 10)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::RectF snippetRect(cardRect.X + Scale(hwnd, 12), cardRect.Y + Scale(hwnd, 28), cardRect.Width - Scale(hwnd, 24), cardRect.Height - Scale(hwnd, 32));
            Gdiplus::StringFormat snippetSf;
            snippetSf.SetTrimming(Gdiplus::StringTrimmingEllipsisWord);
            g.DrawString(bodySnippet.c_str(), -1, &bodyFont, snippetRect, &snippetSf, &inkBrush);

            cardY += cardH + cardGap;
        }
    }

    // Footer: [+ New note] and [All notes]
    int footerY = client.bottom - Scale(hwnd, 44);
    int btnH = Scale(hwnd, 30);
    int btnW = (cardW - Scale(hwnd, 10)) / 2;

    // + New note button
    Gdiplus::RectF newBtnRect(static_cast<float>(pad), static_cast<float>(footerY), static_cast<float>(btnW), static_cast<float>(btnH));
    Gdiplus::GraphicsPath newBtnPath;
    AddRoundedRectToPath(newBtnPath, newBtnRect, static_cast<float>(Scale(hwnd, 5)));
    Gdiplus::SolidBrush btnBgBrush(ToGdiColor(NightPanel));
    g.FillPath(&btnBgBrush, &newBtnPath);
    Gdiplus::Pen btnBorder(ToGdiColor(NightBorder), 1.0f);
    g.DrawPath(&btnBorder, &newBtnPath);
    g.DrawString(L"+ New note", -1, &countFont, newBtnRect, &centerSf, &textBrush);

    // All notes button
    Gdiplus::RectF allBtnRect(static_cast<float>(pad + btnW + Scale(hwnd, 10)), static_cast<float>(footerY), static_cast<float>(btnW), static_cast<float>(btnH));
    Gdiplus::GraphicsPath allBtnPath;
    AddRoundedRectToPath(allBtnPath, allBtnRect, static_cast<float>(Scale(hwnd, 5)));
    g.FillPath(&btnBgBrush, &allBtnPath);
    g.DrawPath(&btnBorder, &allBtnPath);
    g.DrawString(L"All notes", -1, &countFont, allBtnRect, &centerSf, &textBrush);

    buffer.end(hdc);
    EndPaint(hwnd, &ps);
}

int HitTestPeek(HWND hwnd, int x, int y) {
    RECT client;
    GetClientRect(hwnd, &client);
    int pad = Scale(hwnd, 16);
    int topH = Scale(hwnd, 44);
    int cardY = topH + Scale(hwnd, 4);
    int cardH = Scale(hwnd, 82);
    int cardGap = Scale(hwnd, 8);
    int cardW = client.right - pad * 2;

    // Close button
    if (x >= client.right - pad - Scale(hwnd, 24) && x <= client.right - pad + Scale(hwnd, 4) && y >= Scale(hwnd, 8) && y <= Scale(hwnd, 36)) {
        return -2; // Close
    }

    // Cards
    auto notes = GetNotesForActiveDeck();
    if (notes.empty()) {
        if (x >= pad && x <= pad + cardW && y >= cardY && y <= cardY + cardH) {
            return 100; // Fresh note
        }
    } else {
        for (std::size_t i = 0; i < notes.size(); ++i) {
            if (x >= pad && x <= pad + cardW && y >= cardY && y <= cardY + cardH) {
                return static_cast<int>(i); // Card index
            }
            cardY += cardH + cardGap;
        }
    }

    // Footer buttons
    int footerY = client.bottom - Scale(hwnd, 44);
    int btnH = Scale(hwnd, 30);
    int btnW = (cardW - Scale(hwnd, 10)) / 2;

    if (y >= footerY && y <= footerY + btnH) {
        if (x >= pad && x <= pad + btnW) return 200; // + New note
        if (x >= pad + btnW + Scale(hwnd, 10) && x <= pad + cardW) return 201; // All notes
    }

    return -1;
}

LRESULT CALLBACK PeekWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            PaintPeek(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            int hit = HitTestPeek(hwnd, x, y);
            int cardIndex = (hit >= 0 && hit < 100) ? hit : -1;
            if (cardIndex != hoveredPeekCard) {
                hoveredPeekCard = cardIndex;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE: {
            hoveredPeekCard = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONUP: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            int hit = HitTestPeek(hwnd, x, y);

            if (hit == -2) {
                SetAppState(AppState::Collapsed);
                return 0;
            }
            if (hit >= 0 && hit < 100) {
                auto notes = GetNotesForActiveDeck();
                if (static_cast<std::size_t>(hit) < notes.size()) {
                    activeId = notes[hit]->id;
                    SetAppState(AppState::Editing);
                }
                return 0;
            }
            if (hit == 100 || hit == 200) {
                ExecuteCommand(CmdNewNote);
                return 0;
            }
            if (hit == 201) {
                ExecuteCommand(CmdAllNotes);
                return 0;
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                SetAppState(AppState::Collapsed);
                return 0;
            }
            break;

        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && !config.pinned) {
                HWND target = reinterpret_cast<HWND>(lp);
                if (target != dockHwnd && target != editorHwnd && target != managerHwnd) {
                    SetAppState(AppState::Collapsed);
                }
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// 3. EDITOR WINDOW PAINT & LOGIC
// ---------------------------------------------------------------------------
void LayoutEditorControls(HWND hwnd) {
    if (!titleBoxHwnd || !richEditHwnd || !statusBoxHwnd) return;
    RECT client;
    GetClientRect(hwnd, &client);

    int pad = Scale(hwnd, 20);
    int topH = Scale(hwnd, 40);
    int titleH = Scale(hwnd, 36);
    int metaH = Scale(hwnd, 20);
    int toolbarH = Scale(hwnd, 34);
    int statusH = Scale(hwnd, 26);

    int titleY = topH + Scale(hwnd, 4);
    int metaY = titleY + titleH + Scale(hwnd, 2);
    int toolbarY = metaY + metaH + Scale(hwnd, 6);
    int richY = toolbarY + toolbarH + Scale(hwnd, 8);
    int richH = client.bottom - richY - statusH - Scale(hwnd, 10);
    richH = std::max(Scale(hwnd, 50), richH);

    MoveWindow(titleBoxHwnd, pad, titleY, client.right - pad * 2, titleH, TRUE);
    MoveWindow(richEditHwnd, pad, richY, client.right - pad * 2, richH, TRUE);
    MoveWindow(statusBoxHwnd, pad, client.bottom - statusH - Scale(hwnd, 4), client.right - pad * 2, statusH, TRUE);
}

void PaintEditor(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);

    DoubleBuffer buffer;
    buffer.begin(hdc, client.right, client.bottom);
    Gdiplus::Graphics g(buffer.hdcMem);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    auto* n = GetActiveNote();
    COLORREF paperColor = n ? PaperColors[n->color % 8] : PaperColors[0];

    // Entire sheet background in pastel paper color
    Gdiplus::SolidBrush bgBrush(ToGdiColor(paperColor));
    g.FillRectangle(&bgBrush, 0, 0, client.right, client.bottom);

    int pad = Scale(hwnd, 20);

    // 1. Topbar: [Back] | "EdgeNotes > Title" | [More ...] [Export] [Pin] [X]
    Gdiplus::Font navFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 11)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush mutedBrush(ToGdiColor(PaperMuted));
    Gdiplus::SolidBrush inkBrush(ToGdiColor(PaperInk));
    Gdiplus::StringFormat centerSf;
    centerSf.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    // Back to deck button (arrow / chevron)
    Gdiplus::RectF backBtnRect(static_cast<float>(pad), static_cast<float>(Scale(hwnd, 8)), static_cast<float>(Scale(hwnd, 24)), static_cast<float>(Scale(hwnd, 24)));
    Gdiplus::GraphicsPath backBtnPath;
    AddRoundedRectToPath(backBtnPath, backBtnRect, static_cast<float>(Scale(hwnd, 4)));
    Gdiplus::SolidBrush btnBg(ToGdiColor(NightSurface, (hoveredToolbarBtn == CmdBackToDeck ? 45 : 20)));
    g.FillPath(&btnBg, &backBtnPath);
    g.DrawString(L"\x2190", -1, &navFont, backBtnRect, &centerSf, &inkBrush);

    // Breadcrumb text: "● DeckName » [Title]"
    Gdiplus::SolidBrush deckDotBrush(ToGdiColor(DeckList[activeDeck].color));
    float dotX = static_cast<float>(pad + Scale(hwnd, 32));
    float dotY = static_cast<float>(Scale(hwnd, 17));
    float dotSize = static_cast<float>(Scale(hwnd, 7));
    g.FillEllipse(&deckDotBrush, dotX, dotY, dotSize, dotSize);

    std::wstring deckName = DeckList[activeDeck].name;
    if (deckName.size() > 1) {
        for (size_t k = 1; k < deckName.size(); ++k) deckName[k] = towlower(deckName[k]);
    }
    std::wstring breadcrumb = deckName + L"   \x00BB   " + Wide(n ? (n->title.empty() ? "Untitled note" : n->title) : "");
    g.DrawString(breadcrumb.c_str(), -1, &navFont, Gdiplus::PointF(dotX + dotSize + static_cast<float>(Scale(hwnd, 6)), static_cast<float>(Scale(hwnd, 13))), &mutedBrush);

    // Top right buttons: More (…), Export, Pin, Close
    int topRBtnW = Scale(hwnd, 24);
    int topRBtnH = Scale(hwnd, 24);
    int topRBtnY = Scale(hwnd, 8);

    // Close button
    Gdiplus::RectF closeRect(static_cast<float>(client.right - pad - topRBtnW), static_cast<float>(topRBtnY), static_cast<float>(topRBtnW), static_cast<float>(topRBtnH));
    Gdiplus::GraphicsPath closePath;
    AddRoundedRectToPath(closePath, closeRect, static_cast<float>(Scale(hwnd, 4)));
    if (hoveredToolbarBtn == CmdExitApp) {
        Gdiplus::SolidBrush hovB(ToGdiColor(PaperInk, 35));
        g.FillPath(&hovB, &closePath);
    }
    g.DrawString(L"\x2715", -1, &navFont, closeRect, &centerSf, &inkBrush);

    // Pin button
    Gdiplus::RectF pinRect(static_cast<float>(client.right - pad - topRBtnW * 2 - Scale(hwnd, 4)), static_cast<float>(topRBtnY), static_cast<float>(topRBtnW), static_cast<float>(topRBtnH));
    Gdiplus::GraphicsPath pinPath;
    AddRoundedRectToPath(pinPath, pinRect, static_cast<float>(Scale(hwnd, 4)));
    if (config.pinned || hoveredToolbarBtn == CmdPinEditor) {
        Gdiplus::SolidBrush pinActiveBrush(ToGdiColor(PaperInk, config.pinned ? 40 : 25));
        g.FillPath(&pinActiveBrush, &pinPath);
    }
    g.DrawString(config.pinned ? L"\x2297" : L"\x2299", -1, &navFont, pinRect, &centerSf, &inkBrush);

    // Export button
    Gdiplus::RectF exportTopRect(static_cast<float>(client.right - pad - topRBtnW * 3 - Scale(hwnd, 8)), static_cast<float>(topRBtnY), static_cast<float>(topRBtnW), static_cast<float>(topRBtnH));
    Gdiplus::GraphicsPath exportTopPath;
    AddRoundedRectToPath(exportTopPath, exportTopRect, static_cast<float>(Scale(hwnd, 4)));
    if (hoveredToolbarBtn == CmdExportNote) {
        Gdiplus::SolidBrush expHovB(ToGdiColor(PaperInk, 30));
        g.FillPath(&expHovB, &exportTopPath);
    }
    g.DrawString(L"\x2913", -1, &navFont, exportTopRect, &centerSf, &inkBrush);

    // More button (…)
    Gdiplus::RectF moreRect(static_cast<float>(client.right - pad - topRBtnW * 4 - Scale(hwnd, 12)), static_cast<float>(topRBtnY), static_cast<float>(topRBtnW), static_cast<float>(topRBtnH));
    Gdiplus::GraphicsPath morePath;
    AddRoundedRectToPath(morePath, moreRect, static_cast<float>(Scale(hwnd, 4)));
    if (hoveredToolbarBtn == CmdMoreMenu) {
        Gdiplus::SolidBrush moreHovB(ToGdiColor(PaperInk, 30));
        g.FillPath(&moreHovB, &morePath);
    }
    g.DrawString(L"\x2026", -1, &navFont, moreRect, &centerSf, &inkBrush);

    // 2. Metadata line under Title
    int metaY = Scale(hwnd, 44) + Scale(hwnd, 36) + Scale(hwnd, 2);
    Gdiplus::Font metaFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 10)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    std::wstring metaText = L"Local note \x00B7 " + std::to_wstring(n ? Wide(n->body).size() : 0) + L" characters";
    g.DrawString(metaText.c_str(), -1, &metaFont, Gdiplus::PointF(static_cast<float>(pad), static_cast<float>(metaY)), &mutedBrush);

    // 3. Modern Flat Toolbar
    int toolbarY = metaY + Scale(hwnd, 20) + Scale(hwnd, 6);
    int toolbarH = Scale(hwnd, 32);

    // Flat toolbar pill background
    Gdiplus::RectF toolbarRect(static_cast<float>(pad), static_cast<float>(toolbarY), static_cast<float>(client.right - pad * 2), static_cast<float>(toolbarH));
    Gdiplus::GraphicsPath toolbarPath;
    AddRoundedRectToPath(toolbarPath, toolbarRect, static_cast<float>(Scale(hwnd, 6)));
    Gdiplus::SolidBrush toolbarBg(ToGdiColor(PaperInk, 18));
    g.FillPath(&toolbarBg, &toolbarPath);

    // Full tools array
    struct ToolBtn { int id; const wchar_t* label; };
    ToolBtn tools[] = {
        {CmdBoldText, L"B"},
        {CmdItalicText, L"I"},
        {CmdStrikeText, L"S"},
        {CmdHeading1, L"H1"},
        {CmdHeading2, L"H2"},
        {CmdHeading3, L"H3"},
        {CmdBulletText, L"\x2022"},
        {CmdChecklistText, L"\x25A2"},
        {CmdNumberedList, L"1."},
        {CmdCodeBlock, L"</>"},
        {CmdInsertLink, L"\x221E"},
        {CmdUndoText, L"\x21B6"},
        {CmdRedoText, L"\x21B7"}
    };

    int btnX = pad + Scale(hwnd, 4);
    int btnW = Scale(hwnd, 24);
    int btnH = Scale(hwnd, 22);
    int btnY = toolbarY + Scale(hwnd, 5);

    Gdiplus::Font btnFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 10)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    for (const auto& tb : tools) {
        Gdiplus::RectF tr(static_cast<float>(btnX), static_cast<float>(btnY), static_cast<float>(btnW), static_cast<float>(btnH));
        if (hoveredToolbarBtn == tb.id) {
            Gdiplus::GraphicsPath tp;
            AddRoundedRectToPath(tp, tr, static_cast<float>(Scale(hwnd, 3)));
            Gdiplus::SolidBrush hovB(ToGdiColor(PaperInk, 35));
            g.FillPath(&hovB, &tp);
        }
        g.DrawString(tb.label, -1, &btnFont, tr, &centerSf, &inkBrush);
        btnX += btnW + Scale(hwnd, 2);
    }

    // Divider
    btnX += Scale(hwnd, 2);
    Gdiplus::Pen divPen(ToGdiColor(PaperInk, 30), 1.0f);
    g.DrawLine(&divPen, btnX, btnY + 2, btnX, btnY + btnH - 2);
    btnX += Scale(hwnd, 6);

    // 8 Pastel color swatches
    int swatchSize = Scale(hwnd, 13);
    int swatchY = btnY + (btnH - swatchSize) / 2;
    for (int c = 0; c < 8; ++c) {
        Gdiplus::RectF sr(static_cast<float>(btnX), static_cast<float>(swatchY), static_cast<float>(swatchSize), static_cast<float>(swatchSize));
        Gdiplus::SolidBrush sBrush(ToGdiColor(PaperColors[c]));
        g.FillEllipse(&sBrush, sr);
        if (n && (n->color % 8) == static_cast<unsigned>(c)) {
            Gdiplus::Pen selPen(ToGdiColor(PaperInk), 2.0f);
            g.DrawEllipse(&selPen, sr);
        } else if (hoveredToolbarBtn == CmdColorBase + c) {
            Gdiplus::Pen hovPen(ToGdiColor(PaperInk, 120), 1.5f);
            g.DrawEllipse(&hovPen, sr);
        } else {
            Gdiplus::Pen sPen(ToGdiColor(PaperBorder), 1.0f);
            g.DrawEllipse(&sPen, sr);
        }
        btnX += swatchSize + Scale(hwnd, 5);
    }

    // Right-aligned action buttons: Archive, Delete
    int actionX = client.right - pad - Scale(hwnd, 4);
    ToolBtn actions[] = {
        {CmdDeleteNote, L"\x2715"},  // Delete
        {CmdRestoreNote, L"\x25A4"}  // Archive
    };

    for (const auto& act : actions) {
        actionX -= btnW;
        Gdiplus::RectF ar(static_cast<float>(actionX), static_cast<float>(btnY), static_cast<float>(btnW), static_cast<float>(btnH));
        if (hoveredToolbarBtn == act.id) {
            Gdiplus::GraphicsPath ap;
            AddRoundedRectToPath(ap, ar, static_cast<float>(Scale(hwnd, 3)));
            Gdiplus::SolidBrush hovB(ToGdiColor(PaperInk, 35));
            g.FillPath(&hovB, &ap);
        }
        g.DrawString(act.label, -1, &btnFont, ar, &centerSf, &inkBrush);
        actionX -= Scale(hwnd, 4);
    }

    // 4. Delete Undo bar (E12)
    if (store.deleted && GetTickCount64() < store.deleted->expires) {
        int statusH = Scale(hwnd, 26);
        Gdiplus::RectF delBar(static_cast<float>(pad), static_cast<float>(client.bottom - statusH - Scale(hwnd, 28)), static_cast<float>(client.right - pad * 2), static_cast<float>(Scale(hwnd, 22)));
        Gdiplus::GraphicsPath delPath;
        AddRoundedRectToPath(delPath, delBar, static_cast<float>(Scale(hwnd, 4)));
        Gdiplus::SolidBrush delBg(ToGdiColor(PaperInk, (hoveredToolbarBtn == CmdUndoDelete ? 40 : 25)));
        g.FillPath(&delBg, &delPath);
        g.DrawString(L"Note deleted \x00B7 Click here to Undo (Ctrl+Z)", -1, &navFont, delBar, &centerSf, &inkBrush);
    }

    buffer.end(hdc);
    EndPaint(hwnd, &ps);
}

int HitTestEditor(HWND hwnd, int x, int y) {
    RECT client;
    GetClientRect(hwnd, &client);
    int pad = Scale(hwnd, 20);
    int topRBtnW = Scale(hwnd, 24);
    int topRBtnH = Scale(hwnd, 24);
    int topRBtnY = Scale(hwnd, 8);

    // Topbar back button
    if (x >= pad && x <= pad + Scale(hwnd, 24) && y >= topRBtnY && y <= topRBtnY + topRBtnH) {
        return CmdBackToDeck;
    }
    // Close button
    if (x >= client.right - pad - topRBtnW && x <= client.right - pad && y >= topRBtnY && y <= topRBtnY + topRBtnH) {
        return CmdExitApp; // Will collapse editor
    }
    // Pin button
    if (x >= client.right - pad - topRBtnW * 2 - Scale(hwnd, 4) && x <= client.right - pad - topRBtnW - Scale(hwnd, 4) && y >= topRBtnY && y <= topRBtnY + topRBtnH) {
        return CmdPinEditor;
    }
    // Export button
    if (x >= client.right - pad - topRBtnW * 3 - Scale(hwnd, 8) && x <= client.right - pad - topRBtnW * 2 - Scale(hwnd, 8) && y >= topRBtnY && y <= topRBtnY + topRBtnH) {
        return CmdExportNote;
    }
    // More button (…)
    if (x >= client.right - pad - topRBtnW * 4 - Scale(hwnd, 12) && x <= client.right - pad - topRBtnW * 3 - Scale(hwnd, 12) && y >= topRBtnY && y <= topRBtnY + topRBtnH) {
        return CmdMoreMenu;
    }

    // Delete undo bar hit test
    if (store.deleted && GetTickCount64() < store.deleted->expires) {
        int statusH = Scale(hwnd, 26);
        int barY = client.bottom - statusH - Scale(hwnd, 28);
        if (x >= pad && x <= client.right - pad && y >= barY && y <= barY + Scale(hwnd, 22)) {
            return CmdUndoDelete;
        }
    }

    // Toolbar hit testing
    int metaY = Scale(hwnd, 44) + Scale(hwnd, 36) + Scale(hwnd, 2);
    int toolbarY = metaY + Scale(hwnd, 20) + Scale(hwnd, 6);
    int toolbarH = Scale(hwnd, 32);

    if (y >= toolbarY && y <= toolbarY + toolbarH) {
        int btnX = pad + Scale(hwnd, 4);
        int btnW = Scale(hwnd, 24);
        int btnH = Scale(hwnd, 22);
        int btnY = toolbarY + Scale(hwnd, 5);

        int tools[] = {
            CmdBoldText, CmdItalicText, CmdStrikeText,
            CmdHeading1, CmdHeading2, CmdHeading3,
            CmdBulletText, CmdChecklistText, CmdNumberedList,
            CmdCodeBlock, CmdInsertLink, CmdUndoText, CmdRedoText
        };
        for (int cmd : tools) {
            if (x >= btnX && x <= btnX + btnW && y >= btnY && y <= btnY + btnH) return cmd;
            btnX += btnW + Scale(hwnd, 2);
        }

        // Color swatches
        btnX += Scale(hwnd, 8);
        int swatchSize = Scale(hwnd, 13);
        for (int c = 0; c < 8; ++c) {
            if (x >= btnX - 2 && x <= btnX + swatchSize + 2) {
                return CmdColorBase + c;
            }
            btnX += swatchSize + Scale(hwnd, 5);
        }

        // Right actions
        int actionX = client.right - pad - Scale(hwnd, 4);
        int actions[] = {CmdDeleteNote, CmdRestoreNote};
        for (int act : actions) {
            actionX -= btnW;
            if (x >= actionX && x <= actionX + btnW && y >= btnY && y <= btnY + btnH) return act;
            actionX -= Scale(hwnd, 4);
        }
    }

    return 0;
}

LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            titleBoxHwnd = CreateWindowExW(
                0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                20, 48, 500, 36, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)), appInstance, nullptr);
            SendMessageW(titleBoxHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
            SendMessageW(titleBoxHwnd, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Title"));
            SendMessageW(titleBoxHwnd, EM_LIMITTEXT, 200, 0);

            richEditHwnd = CreateWindowExW(
                0, MSFTEDIT_CLASS, L"",
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                20, 140, 500, 380, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(2)), appInstance, nullptr);
            SendMessageW(richEditHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(handFont), TRUE);
            SendMessageW(richEditHwnd, EM_SETEVENTMASK, 0, ENM_CHANGE);
            SendMessageW(richEditHwnd, EM_EXLIMITTEXT, 0, 16 * 1024 * 1024);

            statusBoxHwnd = CreateWindowExW(
                0, L"STATIC", L"Saved locally",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                20, 540, 500, 24, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(3)), appInstance, nullptr);
            SendMessageW(statusBoxHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);

            SetWindowSubclass(richEditHwnd, EditSubclassProc, 1, 0);
            SetWindowSubclass(titleBoxHwnd, EditSubclassProc, 1, 0);
            return 0;
        }

        case WM_SIZE:
            LayoutEditorControls(hwnd);
            return 0;

        case WM_PAINT:
            PaintEditor(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_SETCURSOR: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            int hit = HitTestEditor(hwnd, pt.x, pt.y);
            if (hit != 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            int hit = HitTestEditor(hwnd, x, y);
            if (hit != hoveredToolbarBtn) {
                hoveredToolbarBtn = hit;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE: {
            if (hoveredToolbarBtn != 0) {
                hoveredToolbarBtn = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            auto* n = GetActiveNote();
            COLORREF paperColor = n ? PaperColors[n->color % 8] : PaperColors[0];
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, paperColor);
            SetTextColor(dc, (hwnd == statusBoxHwnd) ? PaperMuted : PaperInk);

            static HBRUSH s_brush = nullptr;
            if (s_brush) DeleteObject(s_brush);
            s_brush = CreateSolidBrush(paperColor);
            return reinterpret_cast<LRESULT>(s_brush);
        }

        case WM_COMMAND: {
            if (HIWORD(wp) == EN_CHANGE && !isLoadingEditor) {
                SaveEditorContent();
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            int cmd = HitTestEditor(hwnd, x, y);
            if (cmd == CmdExitApp) {
                SetAppState(AppState::Collapsed);
            } else if (cmd != 0) {
                ExecuteCommand(cmd);
            }
            return 0;
        }

        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && !config.pinned && !isModalOpen) {
                HWND target = reinterpret_cast<HWND>(lp);
                if (target != dockHwnd && target != peekHwnd && target != managerHwnd) {
                    SetAppState(AppState::Collapsed);
                }
            }
            return 0;

        case WM_CLOSE:
            SetAppState(AppState::Collapsed);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// 4. ALL-NOTES MANAGER WINDOW PAINT & LOGIC
// ---------------------------------------------------------------------------
void RefreshManagerList() {
    managerRows.clear();
    std::wstring query = searchBoxHwnd ? GetWindowTextString(searchBoxHwnd) : L"";
    std::transform(query.begin(), query.end(), query.begin(), [](wchar_t c) { return std::towlower(c); });

    for (const auto& n : store.notes) {
        if (managerFilter == CmdFilterDeck && n.archived) continue;
        if (managerFilter == CmdFilterArchive && !n.archived) continue;
        if (managerFilter == CmdFilterTasks && (n.archived || n.color == 1 || n.color == 4)) continue;
        if (managerFilter == CmdFilterNotes && (n.archived || n.color != 1)) continue;
        if (managerFilter == CmdFilterThoughts && (n.archived || n.color != 4)) continue;

        auto haystack = Wide(n.title + " " + n.body);
        std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](wchar_t c) { return std::towlower(c); });
        if (!query.empty() && haystack.find(query) == std::wstring::npos) continue;

        managerRows.push_back(n.id);
    }

    if (managerSortMode == 0) {
        // Recently edited (ID desc)
        std::sort(managerRows.begin(), managerRows.end(), [](std::uint64_t a, std::uint64_t b) {
            return a > b;
        });
    } else if (managerSortMode == 1) {
        // Title A-Z
        std::sort(managerRows.begin(), managerRows.end(), [](std::uint64_t a, std::uint64_t b) {
            auto* na = GetNoteById(a);
            auto* nb = GetNoteById(b);
            std::string ta = na ? na->title : "";
            std::string tb = nb ? nb->title : "";
            return _stricmp(ta.c_str(), tb.c_str()) < 0;
        });
    } else if (managerSortMode == 2) {
        // My order (ID asc)
        std::sort(managerRows.begin(), managerRows.end(), [](std::uint64_t a, std::uint64_t b) {
            return a < b;
        });
    }

    if (!managerRows.empty()) {
        if (std::find(managerRows.begin(), managerRows.end(), managerSelectedId) == managerRows.end()) {
            managerSelectedId = managerRows.front();
        }
    } else {
        managerSelectedId = 0;
    }

    if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE);
}

void PaintManager(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);

    DoubleBuffer buffer;
    buffer.begin(hdc, client.right, client.bottom);
    Gdiplus::Graphics g(buffer.hdcMem);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // Night Surface background
    Gdiplus::SolidBrush bgBrush(ToGdiColor(NightSurface));
    g.FillRectangle(&bgBrush, 0, 0, client.right, client.bottom);

    int pad = Scale(hwnd, 16);
    int topH = Scale(hwnd, 50);

    // 1. Header: Layers logo, "EdgeNotes", "All your thoughts, together."
    Gdiplus::Font logoFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 14)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush mintBrush(ToGdiColor(PaperColors[0]));
    g.FillEllipse(&mintBrush, pad, Scale(hwnd, 18), Scale(hwnd, 10), Scale(hwnd, 10));

    Gdiplus::SolidBrush inkBrush(ToGdiColor(NightInk));
    g.DrawString(L"EdgeNotes", -1, &logoFont, Gdiplus::PointF(static_cast<float>(pad + Scale(hwnd, 16)), static_cast<float>(Scale(hwnd, 14))), &inkBrush);

    Gdiplus::Font subFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 10)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush mutedBrush(ToGdiColor(NightMuted));
    g.DrawString(L"All your thoughts, together.", -1, &subFont, Gdiplus::PointF(static_cast<float>(pad + Scale(hwnd, 96)), static_cast<float>(Scale(hwnd, 16))), &mutedBrush);

    // Header divider line
    Gdiplus::Pen borderPen(ToGdiColor(NightBorder), 1.0f);
    g.DrawLine(&borderPen, 0, topH, client.right, topH);

    // 2. Three-column layout
    int col1W = Scale(hwnd, 180); // Nav
    int col2W = Scale(hwnd, 320); // List
    int col3W = client.right - col1W - col2W; // Preview

    int bodyY = topH + 1;
    int bodyH = client.bottom - bodyY;

    // Col 1: Navigation Filters
    int navY = bodyY + Scale(hwnd, 14);
    int btnH = Scale(hwnd, 30);

    // [+ New note] mint button
    Gdiplus::RectF newBtn(static_cast<float>(pad), static_cast<float>(navY), static_cast<float>(col1W - pad * 2), static_cast<float>(btnH));
    Gdiplus::GraphicsPath newPath;
    AddRoundedRectToPath(newPath, newBtn, static_cast<float>(Scale(hwnd, 6)));
    g.FillPath(&mintBrush, &newPath);
    Gdiplus::Font newBtnFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 11)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush darkText(ToGdiColor(PaperInk));
    Gdiplus::StringFormat centerSf;
    centerSf.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    g.DrawString(L"+ New note", -1, &newBtnFont, newBtn, &centerSf, &darkText);

    navY += btnH + Scale(hwnd, 12);

    // Counts
    std::size_t deckCount = 0, archCount = 0, tasksCount = 0, notesCount = 0, thoughtsCount = 0;
    for (const auto& n : store.notes) {
        if (n.archived) {
            archCount++;
        } else {
            deckCount++;
            if (n.color == 1) notesCount++;
            else if (n.color == 4) thoughtsCount++;
            else tasksCount++;
        }
    }

    struct NavItem { int id; const wchar_t* label; std::size_t count; bool isSub; };
    NavItem navs[] = {
        {CmdFilterAll, L"All notes", store.notes.size(), false},
        {CmdFilterDeck, L"On the deck", deckCount, false},
        {CmdFilterTasks, L"\x2611  Tasks", tasksCount, true},
        {CmdFilterNotes, L"\x2630  Notes", notesCount, true},
        {CmdFilterThoughts, L"\x2726  Thoughts", thoughtsCount, true},
        {CmdFilterArchive, L"Archive", archCount, false}
    };

    Gdiplus::Font navItemFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 11)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font navSubFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 10)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

    for (const auto& item : navs) {
        bool selected = (managerFilter == item.id);
        int itemIndent = item.isSub ? Scale(hwnd, 12) : 0;
        int rowH = item.isSub ? Scale(hwnd, 24) : Scale(hwnd, 28);
        Gdiplus::RectF nr(static_cast<float>(pad + itemIndent), static_cast<float>(navY), static_cast<float>(col1W - pad * 2 - itemIndent), static_cast<float>(rowH));
        if (selected) {
            Gdiplus::GraphicsPath selPath;
            AddRoundedRectToPath(selPath, nr, static_cast<float>(Scale(hwnd, 4)));
            Gdiplus::SolidBrush selBg(ToGdiColor(NightSelected));
            g.FillPath(&selBg, &selPath);
        }
        g.DrawString(item.label, -1, item.isSub ? &navSubFont : &navItemFont, Gdiplus::PointF(nr.X + Scale(hwnd, 6), nr.Y + (item.isSub ? 3 : 5)), selected ? &inkBrush : &mutedBrush);

        std::wstring countStr = std::to_wstring(item.count);
        Gdiplus::StringFormat rightSf;
        rightSf.SetAlignment(Gdiplus::StringAlignmentFar);
        rightSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(countStr.c_str(), -1, &subFont, Gdiplus::RectF(nr.X, nr.Y, nr.Width - Scale(hwnd, 6), nr.Height), &rightSf, &mutedBrush);

        navY += rowH + Scale(hwnd, 2);
    }

    // M7: "Saved on this device" at bottom of Col 1
    Gdiplus::Font savedFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 9)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    g.DrawString(L"\x2713 Saved on this device", -1, &savedFont, Gdiplus::PointF(static_cast<float>(pad), static_cast<float>(client.bottom - Scale(hwnd, 26))), &mutedBrush);

    // Col 1 border
    g.DrawLine(&borderPen, col1W, bodyY, col1W, client.bottom);

    // Col 2: Notes List
    int col2X = col1W;
    Gdiplus::SolidBrush panelBg(ToGdiColor(NightPanel));
    g.FillRectangle(&panelBg, col2X, bodyY, col2W, bodyH);

    // Header of Col 2
    int listHeadY = bodyY + Scale(hwnd, 12);
    const wchar_t* listTitle = L"All notes";
    if (managerFilter == CmdFilterDeck) listTitle = L"On the deck";
    else if (managerFilter == CmdFilterTasks) listTitle = L"Tasks";
    else if (managerFilter == CmdFilterNotes) listTitle = L"Notes";
    else if (managerFilter == CmdFilterThoughts) listTitle = L"Thoughts";
    else if (managerFilter == CmdFilterArchive) listTitle = L"Archive";
    g.DrawString(listTitle, -1, &logoFont, Gdiplus::PointF(static_cast<float>(col2X + pad), static_cast<float>(listHeadY)), &inkBrush);

    // Header action buttons: [Import] [Backup] [Sort ▾]
    int actBtnH = Scale(hwnd, 20);
    int rightX = col2X + col2W - pad;

    // Export Backup button (M5)
    int backupW = Scale(hwnd, 24);
    rightX -= backupW;
    Gdiplus::RectF backupRect(static_cast<float>(rightX), static_cast<float>(listHeadY), static_cast<float>(backupW), static_cast<float>(actBtnH));
    g.DrawString(L"\x2913", -1, &navItemFont, backupRect, &centerSf, &mutedBrush);

    // Import button (M4)
    int importW = Scale(hwnd, 24);
    rightX -= (importW + Scale(hwnd, 4));
    Gdiplus::RectF importRect(static_cast<float>(rightX), static_cast<float>(listHeadY), static_cast<float>(importW), static_cast<float>(actBtnH));
    g.DrawString(L"\x2912", -1, &navItemFont, importRect, &centerSf, &mutedBrush);

    // Sort dropdown button (M2)
    const wchar_t* sortLabel = (managerSortMode == 0) ? L"Recent \x25BE" : (managerSortMode == 1 ? L"A-Z \x25BE" : L"Order \x25BE");
    int sortW = Scale(hwnd, 58);
    rightX -= (sortW + Scale(hwnd, 6));
    Gdiplus::RectF sortRect(static_cast<float>(rightX), static_cast<float>(listHeadY), static_cast<float>(sortW), static_cast<float>(actBtnH));
    g.DrawString(sortLabel, -1, &subFont, sortRect, &centerSf, &mutedBrush);

    // Notes list items
    int itemY = bodyY + Scale(hwnd, 72);
    int itemH = Scale(hwnd, 64);
    int itemPad = Scale(hwnd, 8);

    for (std::size_t i = 0; i < managerRows.size(); ++i) {
        if (itemY + itemH > client.bottom) break;

        auto* n = GetNoteById(managerRows[i]);
        if (!n) continue;

        bool isSelected = (managerSelectedId == n->id);
        bool isHovered = (hoveredManagerRow == static_cast<int>(i));

        Gdiplus::RectF ir(static_cast<float>(col2X + itemPad), static_cast<float>(itemY), static_cast<float>(col2W - itemPad * 2), static_cast<float>(itemH));
        if (isSelected) {
            Gdiplus::GraphicsPath ip;
            AddRoundedRectToPath(ip, ir, static_cast<float>(Scale(hwnd, 5)));
            Gdiplus::SolidBrush selB(ToGdiColor(NightSurface));
            g.FillPath(&selB, &ip);
            Gdiplus::Pen selP(ToGdiColor(PaperColors[0], 120), 1.0f);
            g.DrawPath(&selP, &ip);
        } else if (isHovered) {
            Gdiplus::GraphicsPath ip;
            AddRoundedRectToPath(ip, ir, static_cast<float>(Scale(hwnd, 5)));
            Gdiplus::SolidBrush hovB(ToGdiColor(NightSurface, 120));
            g.FillPath(&hovB, &ip);
        }

        // Color stripe
        Gdiplus::RectF stripeRect(ir.X + Scale(hwnd, 6), ir.Y + Scale(hwnd, 12), static_cast<float>(Scale(hwnd, 3)), static_cast<float>(Scale(hwnd, 40)));
        Gdiplus::GraphicsPath stripePath;
        AddRoundedRectToPath(stripePath, stripeRect, static_cast<float>(Scale(hwnd, 1)));
        Gdiplus::SolidBrush stripeBrush(ToGdiColor(PaperColors[n->color % 8]));
        g.FillPath(&stripeBrush, &stripePath);

        // Note title
        std::wstring itemTitle = Wide(n->title.empty() ? "Untitled note" : n->title);
        Gdiplus::Font rowTitleFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 11)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        g.DrawString(itemTitle.c_str(), -1, &rowTitleFont, Gdiplus::PointF(ir.X + Scale(hwnd, 16), ir.Y + Scale(hwnd, 10)), &inkBrush);

        // Snippet
        std::wstring snippet = Wide(n->body.substr(0, 45));
        Gdiplus::Font rowSnippetFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 9)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        g.DrawString(snippet.c_str(), -1, &rowSnippetFont, Gdiplus::PointF(ir.X + Scale(hwnd, 16), ir.Y + Scale(hwnd, 32)), &mutedBrush);

        // M6: Date on rows
        g.DrawString(L"Oct 18", -1, &subFont, Gdiplus::PointF(ir.X + ir.Width - Scale(hwnd, 48), ir.Y + Scale(hwnd, 10)), &mutedBrush);

        itemY += itemH + Scale(hwnd, 4);
    }

    // Col 2 border
    g.DrawLine(&borderPen, col1W + col2W, bodyY, col1W + col2W, client.bottom);

    // Col 3: Preview Panel
    int col3X = col1W + col2W;
    auto* selNote = GetNoteById(managerSelectedId);

    if (selNote) {
        // Status header
        int prevPad = Scale(hwnd, 20);
        int prevHeadY = bodyY + Scale(hwnd, 14);

        std::wstring statTag = selNote->archived ? L"\x25CF ARCHIVED" : L"\x25CF ON THE DECK";
        g.DrawString(statTag.c_str(), -1, &subFont, Gdiplus::PointF(static_cast<float>(col3X + prevPad), static_cast<float>(prevHeadY)), &mutedBrush);

        // Preview Paper Sheet Card
        int sheetY = prevHeadY + Scale(hwnd, 28);
        int sheetW = col3W - prevPad * 2;
        int sheetH = bodyH - Scale(hwnd, 80);

        Gdiplus::RectF sheetRect(static_cast<float>(col3X + prevPad), static_cast<float>(sheetY), static_cast<float>(sheetW), static_cast<float>(sheetH));
        Gdiplus::GraphicsPath sheetPath;
        AddRoundedRectToPath(sheetPath, sheetRect, static_cast<float>(Scale(hwnd, 8)));
        Gdiplus::SolidBrush sheetBg(ToGdiColor(PaperColors[selNote->color % 8]));
        g.FillPath(&sheetBg, &sheetPath);
        Gdiplus::Pen sheetBorder(ToGdiColor(PaperBorder, 100), 1.0f);
        g.DrawPath(&sheetBorder, &sheetPath);

        // Title on sheet
        std::wstring sheetTitle = Wide(selNote->title.empty() ? "Untitled note" : selNote->title);
        Gdiplus::Font sheetTitleFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 18)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush sheetInk(ToGdiColor(PaperInk));
        g.DrawString(sheetTitle.c_str(), -1, &sheetTitleFont, Gdiplus::PointF(sheetRect.X + Scale(hwnd, 18), sheetRect.Y + Scale(hwnd, 18)), &sheetInk);

        // Body on sheet in Segoe Print handwriting
        std::wstring sheetBody = Wide(selNote->body);
        Gdiplus::Font sheetBodyFont(L"Segoe Print", static_cast<float>(Scale(hwnd, 12)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::RectF bodyBound(sheetRect.X + Scale(hwnd, 18), sheetRect.Y + Scale(hwnd, 50), sheetRect.Width - Scale(hwnd, 36), sheetRect.Height - Scale(hwnd, 100));
        g.DrawString(sheetBody.c_str(), -1, &sheetBodyFont, bodyBound, nullptr, &sheetInk);

        // Open Note button on bottom right of sheet
        Gdiplus::RectF openBtnRect(sheetRect.X + sheetRect.Width - Scale(hwnd, 110), sheetRect.Y + sheetRect.Height - Scale(hwnd, 38), static_cast<float>(Scale(hwnd, 96)), static_cast<float>(Scale(hwnd, 26)));
        Gdiplus::GraphicsPath openBtnPath;
        AddRoundedRectToPath(openBtnPath, openBtnRect, static_cast<float>(Scale(hwnd, 4)));
        Gdiplus::SolidBrush openBtnBg(ToGdiColor(NightSurface));
        g.FillPath(&openBtnBg, &openBtnPath);
        g.DrawString(L"Open note \x2197", -1, &newBtnFont, openBtnRect, &centerSf, &mintBrush);
    }

    buffer.end(hdc);
    EndPaint(hwnd, &ps);
}

int HitTestManager(HWND hwnd, int x, int y) {
    RECT client;
    GetClientRect(hwnd, &client);
    int pad = Scale(hwnd, 16);
    int topH = Scale(hwnd, 50);
    int bodyY = topH + 1;
    int bodyH = client.bottom - bodyY;

    int col1W = Scale(hwnd, 180);
    int col2W = Scale(hwnd, 320);
    int col3W = client.right - col1W - col2W;

    // Col 1 hit tests
    if (x >= pad && x <= col1W - pad) {
        int navY = bodyY + Scale(hwnd, 14);
        int btnH = Scale(hwnd, 30);

        // [+ New note]
        if (y >= navY && y <= navY + btnH) return CmdNewNote;
        navY += btnH + Scale(hwnd, 12);

        // Nav filter items
        int filterIds[] = {CmdFilterAll, CmdFilterDeck, CmdFilterTasks, CmdFilterNotes, CmdFilterThoughts, CmdFilterArchive};
        bool isSubs[] = {false, false, true, true, true, false};
        for (int i = 0; i < 6; ++i) {
            int rowH = isSubs[i] ? Scale(hwnd, 24) : Scale(hwnd, 28);
            if (y >= navY && y <= navY + rowH) return filterIds[i];
            navY += rowH + Scale(hwnd, 2);
        }
    }

    // Col 2 hit tests (Header buttons & Notes List)
    if (x >= col1W && x <= col1W + col2W) {
        int listHeadY = bodyY + Scale(hwnd, 12);
        int actBtnH = Scale(hwnd, 20);
        if (y >= listHeadY && y <= listHeadY + actBtnH) {
            int rightX = col1W + col2W - pad;
            int backupW = Scale(hwnd, 24);
            rightX -= backupW;
            if (x >= rightX && x <= rightX + backupW) return CmdExportBackupJson;

            int importW = Scale(hwnd, 24);
            rightX -= (importW + Scale(hwnd, 4));
            if (x >= rightX && x <= rightX + importW) return CmdImportNotes;

            int sortW = Scale(hwnd, 58);
            rightX -= (sortW + Scale(hwnd, 6));
            if (x >= rightX && x <= rightX + sortW) return CmdSortOrder;
        }

        int itemY = bodyY + Scale(hwnd, 72);
        int itemH = Scale(hwnd, 64);
        for (std::size_t i = 0; i < managerRows.size(); ++i) {
            if (y >= itemY && y <= itemY + itemH) {
                return 1000 + static_cast<int>(i); // List row index
            }
            itemY += itemH + Scale(hwnd, 4);
        }
    }

    // Col 3 hit tests (Open note button)
    if (x > col1W + col2W) {
        int col3X = col1W + col2W;
        int prevPad = Scale(hwnd, 20);
        int sheetY = bodyY + Scale(hwnd, 42);
        int sheetW = col3W - prevPad * 2;
        int sheetH = bodyH - Scale(hwnd, 80);
        int btnX = col3X + prevPad + sheetW - Scale(hwnd, 110);
        int btnY = sheetY + sheetH - Scale(hwnd, 38);
        int btnW = Scale(hwnd, 96);
        int btnH = Scale(hwnd, 26);
        if (x >= btnX && x <= btnX + btnW && y >= btnY && y <= btnY + btnH) {
            return 2000; // Open note
        }
    }

    return 0;
}

LRESULT CALLBACK ManagerWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            int col1W = Scale(hwnd, 180);
            int col2W = Scale(hwnd, 320);
            int pad = Scale(hwnd, 12);
            int sbY = Scale(hwnd, 50) + Scale(hwnd, 38);
            int sbH = Scale(hwnd, 26);
            searchBoxHwnd = CreateWindowExW(
                0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
                col1W + pad, sbY, col2W - pad * 2, sbH,
                hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(4)), appInstance, nullptr);
            SendMessageW(searchBoxHwnd, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Find a thought\x2026"));
            SendMessageW(searchBoxHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
            SetWindowTheme(searchBoxHwnd, L"DarkMode_Explorer", nullptr);
            return 0;
        }

        case WM_SIZE: {
            int col1W = Scale(hwnd, 180);
            int col2W = Scale(hwnd, 320);
            int pad = Scale(hwnd, 12);
            int sbY = Scale(hwnd, 50) + Scale(hwnd, 38);
            int sbH = Scale(hwnd, 26);
            if (searchBoxHwnd) {
                SetWindowPos(searchBoxHwnd, nullptr, col1W + pad, sbY, col2W - pad * 2, sbH, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_PAINT:
            PaintManager(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_SETCURSOR: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            int hit = HitTestManager(hwnd, pt.x, pt.y);
            if (hit != 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, NightPanel);
            SetTextColor(dc, NightInk);
            static HBRUSH s_mgrBrush = nullptr;
            if (s_mgrBrush) DeleteObject(s_mgrBrush);
            s_mgrBrush = CreateSolidBrush(NightPanel);
            return reinterpret_cast<LRESULT>(s_mgrBrush);
        }

        case WM_COMMAND: {
            if (LOWORD(wp) == 4 && HIWORD(wp) == EN_CHANGE) {
                RefreshManagerList();
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            int hit = HitTestManager(hwnd, x, y);
            int row = (hit >= 1000 && hit < 2000) ? (hit - 1000) : -1;
            if (row != hoveredManagerRow) {
                hoveredManagerRow = row;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            int hit = HitTestManager(hwnd, x, y);

            if (hit == CmdNewNote) {
                ShowWindow(hwnd, SW_HIDE);
                ExecuteCommand(CmdNewNote);
                return 0;
            }
            if (hit == CmdFilterAll || hit == CmdFilterDeck || hit == CmdFilterTasks || hit == CmdFilterNotes || hit == CmdFilterThoughts || hit == CmdFilterArchive) {
                managerFilter = hit;
                RefreshManagerList();
                return 0;
            }
            if (hit == CmdImportNotes) {
                ImportNotesFromFile();
                return 0;
            }
            if (hit == CmdExportBackupJson) {
                ExportBackupJson();
                return 0;
            }
            if (hit == CmdSortOrder) {
                HMENU sm = CreatePopupMenu();
                AppendMenuW(sm, MF_STRING | (managerSortMode == 0 ? MF_CHECKED : 0), CmdSortRecentlyEdited, L"Recently edited");
                AppendMenuW(sm, MF_STRING | (managerSortMode == 1 ? MF_CHECKED : 0), CmdSortTitle, L"Title A\x2013Z");
                AppendMenuW(sm, MF_STRING | (managerSortMode == 2 ? MF_CHECKED : 0), CmdSortOrder, L"My order");
                POINT pt;
                GetCursorPos(&pt);
                int choice = TrackPopupMenu(sm, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
                DestroyMenu(sm);
                if (choice) ExecuteCommand(choice);
                return 0;
            }
            if (hit >= 1000 && hit < 2000) {
                std::size_t idx = static_cast<std::size_t>(hit - 1000);
                if (idx < managerRows.size()) {
                    managerSelectedId = managerRows[idx];
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
                return 0;
            }
            if (hit == 2000) {
                if (managerSelectedId != 0) {
                    activeId = managerSelectedId;
                    ShowWindow(hwnd, SW_HIDE);
                    SetAppState(AppState::Editing);
                }
                return 0;
            }
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            int hit = HitTestManager(hwnd, x, y);
            if (hit >= 1000 && hit < 2000) {
                std::size_t idx = static_cast<std::size_t>(hit - 1000);
                if (idx < managerRows.size()) {
                    activeId = managerRows[idx];
                    ShowWindow(hwnd, SW_HIDE);
                    SetAppState(AppState::Editing);
                }
                return 0;
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    appInstance = inst;

    // 1. High-DPI awareness (Per-Monitor V2)
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        typedef BOOL(WINAPI * SetProcessDpiContextFn)(DPI_AWARENESS_CONTEXT);
        auto pfn = reinterpret_cast<SetProcessDpiContextFn>(GetProcAddress(hUser, "SetProcessDpiAwarenessContext"));
        if (pfn) pfn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        else SetProcessDPIAware();
    } else {
        SetProcessDPIAware();
    }

    // 2. Single instance check & activation
    WM_ACTIVATE_INSTANCE = RegisterWindowMessageW(L"EdgeNotes_ActivateInstance");
    WM_TASKBAR_CREATED = RegisterWindowMessageW(L"TaskbarCreated");

    HANDLE singleton = CreateMutexW(nullptr, FALSE, L"Local\\EdgeNotesNativeWindows");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Find existing instance and bring to front
        HWND existingDock = FindWindowW(L"EdgeDock", nullptr);
        if (existingDock) {
            PostMessageW(existingDock, WM_ACTIVATE_INSTANCE, 0, 0);
        } else {
            PostMessageW(HWND_BROADCAST, WM_ACTIVATE_INSTANCE, 0, 0);
        }
        if (singleton) CloseHandle(singleton);
        return 0;
    }

    // 3. Initialize GDI+ and Common Controls
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdiToken;
    Gdiplus::GdiplusStartup(&gdiToken, &gsi, nullptr);

    richLibrary = LoadLibraryW(L"Msftedit.dll");
    if (!richLibrary) return 1;

    INITCOMMONCONTROLSEX icex{sizeof(icex), ICC_STANDARD_CLASSES | ICC_COOL_CLASSES};
    InitCommonControlsEx(&icex);

    // 4. Create typography fonts
    uiFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    uiBoldFont = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    titleFont = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    handFont = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe Print");
    handPreviewFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe Print");
    monoFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");

    // 5. App Data Paths & Storage
    std::wstring baseDir;
    if (CheckIsPortableMode()) {
        baseDir = GetExeDir();
    } else {
        PWSTR localFolder = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localFolder))) return 1;
        baseDir = std::wstring(localFolder) + L"\\EdgeNotes";
        CoTaskMemFree(localFolder);
        CreateDirectoryW(baseDir.c_str(), nullptr);
    }
    storagePath = baseDir + L"\\notes.enotes";
    configPath = baseDir + L"\\config.ini";

    config.load(configPath);

    // Read notes from storage
    HANDLE hNotesFile = CreateFileW(storagePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hNotesFile != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        if (GetFileSizeEx(hNotesFile, &size) && size.QuadPart > 0 && size.QuadPart < 128 * 1024 * 1024) {
            std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
            DWORD readBytes = 0;
            if (ReadFile(hNotesFile, &bytes[0], static_cast<DWORD>(bytes.size()), &readBytes, nullptr) && (readBytes == bytes.size())) {
                try {
                    store.notes = edgenotes::decode(bytes);
                } catch (...) {
                    // Protected corrupt file
                }
            }
        }
        CloseHandle(hNotesFile);
    }

    for (const auto& n : store.notes) {
        nextId = std::max(nextId, n.id + 1);
    }
    activeId = store.notes.empty() ? 0 : store.notes.front().id;

    // 6. Register Window Classes
    WNDCLASSW wcDock{0};
    wcDock.lpfnWndProc = DockWndProc;
    wcDock.hInstance = inst;
    wcDock.lpszClassName = L"EdgeDock";
    wcDock.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&wcDock);

    WNDCLASSW wcPeek{0};
    wcPeek.lpfnWndProc = PeekWndProc;
    wcPeek.hInstance = inst;
    wcPeek.lpszClassName = L"EdgePeek";
    wcPeek.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&wcPeek);

    WNDCLASSW wcEditor{0};
    wcEditor.lpfnWndProc = EditorWndProc;
    wcEditor.hInstance = inst;
    wcEditor.lpszClassName = L"EdgeEditor";
    wcEditor.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&wcEditor);

    WNDCLASSW wcManager{0};
    wcManager.lpfnWndProc = ManagerWndProc;
    wcManager.hInstance = inst;
    wcManager.lpszClassName = L"EdgeManager";
    wcManager.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&wcManager);

    // 7. Create Windows
    dockHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED, L"EdgeDock", L"EdgeNotes Dock",
        WS_POPUP, 0, 0, 44, 750, nullptr, nullptr, inst, nullptr);
    if (dockHwnd) {
        SetLayeredWindowAttributes(dockHwnd, RGB(12, 14, 18), 0, LWA_COLORKEY);
    }

    peekHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"EdgePeek", L"EdgeNotes Peek",
        WS_POPUP, 0, 0, 380, 520, nullptr, nullptr, inst, nullptr);

    editorHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | (config.pinned ? WS_EX_TOPMOST : 0), L"EdgeEditor", L"EdgeNotes",
        WS_POPUP | WS_THICKFRAME, 100, 100, 580, 600, nullptr, nullptr, inst, nullptr);

    managerHwnd = CreateWindowExW(
        0, L"EdgeManager", L"EdgeNotes \x2014 All notes",
        WS_OVERLAPPEDWINDOW, 140, 120, 880, 600, nullptr, nullptr, inst, nullptr);

    if (!dockHwnd || !peekHwnd || !editorHwnd || !managerHwnd) return 2;

    ApplyDwmDarkAndCorners(peekHwnd, false);
    ApplyDwmDarkAndCorners(editorHwnd, false);
    ApplyDwmDarkAndCorners(managerHwnd, true);

    // 8. Tray Icon
    appIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    if (!appIcon) appIcon = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = dockHwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = appIcon;
    lstrcpyW(nid.szTip, L"EdgeNotes - local notes");
    Shell_NotifyIconW(NIM_ADD, &nid);

    // 9. Register Global Hotkeys (Non-blocking collision reporting!)
    bool k1 = RegisterHotKey(dockHwnd, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'N') != 0;
    bool k2 = RegisterHotKey(dockHwnd, 2, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'A') != 0;
    bool k3 = RegisterHotKey(dockHwnd, 3, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'L') != 0; // Show/hide dock
    RegisterHotKey(dockHwnd, 4, MOD_ALT | MOD_NOREPEAT, VK_SPACE); // Alt+Space

    if (!k1 || !k2 || !k3) {
        hotkeyCollisionNotice = "Some global hotkeys are in use by another app";
    }

    SetAppState(AppState::Collapsed, true);

    if (!config.hasRunBefore) {
        config.hasRunBefore = true;
        config.save(configPath);
        PostMessageW(dockHwnd, WM_COMMAND, CmdPromptDesktopShortcut, 0);
    }

    // 10. Win32 Message Loop
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_ACTIVATE_INSTANCE) {
            isDockVisible = true;
            ShowWindow(dockHwnd, SW_SHOW);
            SetAppState(AppState::Peek, true);
            continue;
        }

        if (msg.message == WM_TASKBAR_CREATED) {
            Shell_NotifyIconW(NIM_ADD, &nid);
            continue;
        }

        if (msg.message == WM_HOTKEY) {
            if (msg.wParam == 1) ExecuteCommand(CmdNewNote);
            else if (msg.wParam == 2) ExecuteCommand(CmdAllNotes);
            else if (msg.wParam == 3) ExecuteCommand(CmdToggleDock);
            else if (msg.wParam == 4) {
                SetAppState(appState == AppState::Collapsed ? AppState::Peek : AppState::Collapsed);
            }
            continue;
        }

        if (msg.message == WM_TRAYICON) {
            if (msg.lParam == WM_RBUTTONUP) {
                ShowTrayContextMenu();
            } else if (msg.lParam == WM_LBUTTONUP) {
                SetAppState(appState == AppState::Collapsed ? AppState::Peek : AppState::Collapsed);
            }
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 11. Cleanup
    Shell_NotifyIconW(NIM_DELETE, &nid);
    for (int i = 1; i <= 4; ++i) UnregisterHotKey(dockHwnd, i);

    if (IsWindow(editorHwnd)) DestroyWindow(editorHwnd);
    if (IsWindow(peekHwnd)) DestroyWindow(peekHwnd);
    if (IsWindow(managerHwnd)) DestroyWindow(managerHwnd);
    if (IsWindow(dockHwnd)) DestroyWindow(dockHwnd);

    DeleteObject(uiFont);
    DeleteObject(uiBoldFont);
    DeleteObject(titleFont);
    DeleteObject(handFont);
    DeleteObject(handPreviewFont);
    DeleteObject(monoFont);
    if (tabRotatedFont) DeleteObject(tabRotatedFont);

    FreeLibrary(richLibrary);
    Gdiplus::GdiplusShutdown(gdiToken);
    if (singleton) CloseHandle(singleton);

    return 0;
}