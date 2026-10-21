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
    CmdUnderlineText,
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
    CmdOpenNotesFolder,
    CmdNavNotes = 300,
    CmdNavDeck,
    CmdNavArchive,
    CmdNavSettings,
    CmdNavAbout,
    CmdNavSignIn,
    CmdTrialEnterKey,
    CmdTrialBuy,
    CmdPopoutNote,
    CmdMarkComplete,
    CmdCloseEditor,
    CmdThemeAuto,
    CmdThemeLight,
    CmdThemeDark,
    CmdFontHandwritten,
    CmdFontNormal,
    CmdFontRounded,
    CmdFontTypewriter,
    CmdEdgeLeft,
    CmdEdgeRight,
    CmdToggleFullscreenHide,
    CmdToggleStartWithWindows,
    CmdMinimiseEditor,
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
HWND undoHwnd = nullptr;
HWND titleBoxHwnd = nullptr;
HWND richEditHwnd = nullptr;
HWND statusBoxHwnd = nullptr;
HWND searchBoxHwnd = nullptr;
HWND managerTitleHwnd = nullptr;
HWND managerBodyHwnd = nullptr;

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
int managerNavSection = 0; // 0: Notes (All / Deck / Archive), 1: Settings, 2: About
int appearanceTheme = 0; // 0: Auto, 1: Light, 2: Dark
int noteFontChoice = 0; // 0: Handwritten, 1: Normal, 2: Rounded, 3: Typewriter
bool hideOverFullscreen = true;
bool startWithWindows = false;
std::vector<std::uint64_t> managerRows;
std::uint64_t managerSelectedId = 0;

void LayoutManagerControls(HWND hwnd);
void RefreshManagerList();
void LoadManagerSelectedNote();
void SetRichEditRtf(HWND hEdit, const std::string& rtf);
std::string GetRichEditRtf(HWND hEdit);
std::string Narrow(const std::wstring& s);

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

inline int Scale(HWND hwnd, int val) {
    return static_cast<int>(std::round(static_cast<float>(val) * GetDpiScale(hwnd)));
}

inline float ScaleF(HWND hwnd, float val) {
    return val * GetDpiScale(hwnd);
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

std::string Narrow(const std::wstring& s) {
    return Utf8(s);
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
        if (!n.archived && n.isInDeck) {
            result.push_back(&n);
            if (result.size() >= 5) break;
        }
    }
    if (result.empty()) {
        for (auto& n : store.notes) {
            if (!n.archived) {
                result.push_back(&n);
                if (result.size() >= 5) break;
            }
        }
    }
    return result;
}

std::vector<Note*> GetNotesForActiveDeck() {
    return GetDeckNotes();
}


bool AtomicWriteFile(const std::wstring& path, const std::string& data) {
    auto temp = path + L".tmp";
    auto bak = path + L".bak";
    HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && (written == data.size());
    if (ok) ok = FlushFileBuffers(h) != 0;
    CloseHandle(h);
    if (ok) {
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            CopyFileW(path.c_str(), bak.c_str(), FALSE);
        }
        ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    }
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
void ShowUndoToast();
void PaintUndoToast(HWND hwnd);
LRESULT CALLBACK UndoWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

void PersistStore() {
    try {
        isSaveFailed = !AtomicWriteFile(storagePath, edgenotes::encode(store.notes));
        std::wstring jsonPath = storagePath;
        size_t dotPos = jsonPath.rfind(L'.');
        if (dotPos != std::wstring::npos) jsonPath = jsonPath.substr(0, dotPos) + L".json";
        else jsonPath += L".json";
        AtomicWriteFile(jsonPath, edgenotes::encodeJson(store.notes));
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

void SetRichEditRtf(HWND hEdit, const std::string& rtf) {
    if (!hEdit) return;
    if (rtf.empty()) {
        SetWindowTextW(hEdit, L"");
        return;
    }
    RtfStream stream{rtf};
    EDITSTREAM es{};
    es.dwCookie = reinterpret_cast<DWORD_PTR>(&stream);
    es.pfnCallback = RtfStreamInCallback;
    SendMessageW(hEdit, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es));
}

std::string GetRichEditRtf(HWND hEdit) {
    if (!hEdit) return "";
    RtfStream stream;
    EDITSTREAM es{};
    es.dwCookie = reinterpret_cast<DWORD_PTR>(&stream);
    es.pfnCallback = RtfStreamOutCallback;
    SendMessageW(hEdit, EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&es));
    if (!es.dwError) {
        return std::move(stream.bytes);
    }
    return "";
}

void SaveEditorContent() {
    if (isLoadingEditor) return;
    auto* n = GetActiveNote();
    if (!n) return;
    std::string newTitle = Utf8(GetWindowTextString(titleBoxHwnd));
    bool titleChanged = (n->title != newTitle);
    n->title = newTitle;
    n->displayTitle = newTitle;
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
    if (titleChanged && dockHwnd) {
        InvalidateRect(dockHwnd, nullptr, TRUE);
    }
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

    auto deckNotes = GetDeckNotes();
    int noteCount = static_cast<int>(deckNotes.size());
    int tabsLen = (noteCount > 0) ? (noteCount * (pillLen + gap)) : 0;
    int topGap = (noteCount > 0) ? Scale(dockHwnd, 10) : Scale(dockHwnd, 6);
    int totalLen = gripSize + gap + tabsLen + topGap + (4 * (iconBtnSize + iconGap)) + Scale(dockHwnd, 8);

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

    int peekW = Scale(peekHwnd, 310);
    int peekH = Scale(peekHwnd, 185);

    auto deckNotes = GetDeckNotes();
    int tabIdx = 0;
    if (hoveredTabIndex >= 0 && hoveredTabIndex < static_cast<int>(deckNotes.size())) {
        tabIdx = hoveredTabIndex;
    } else {
        for (std::size_t i = 0; i < deckNotes.size(); ++i) {
            if (deckNotes[i]->id == activeId) {
                tabIdx = static_cast<int>(i);
                break;
            }
        }
    }

    int pillLen = Scale(dockHwnd, 64);
    int gripSize = Scale(dockHwnd, 24);
    int gap = Scale(dockHwnd, 4);

    int tabOffset = gripSize + gap + tabIdx * (pillLen + gap);

    int x = 0, y = 0;
    int margin = Scale(peekHwnd, 6);

    if (config.side == DockSide::Right) {
        x = dockRect.left - peekW - margin;
        y = dockRect.top + tabOffset - Scale(peekHwnd, 8);
    } else if (config.side == DockSide::Left) {
        x = dockRect.right + margin;
        y = dockRect.top + tabOffset - Scale(peekHwnd, 8);
    } else if (config.side == DockSide::Top) {
        x = dockRect.left + tabOffset - Scale(peekHwnd, 8);
        y = dockRect.bottom + margin;
    } else { // Bottom
        x = dockRect.left + tabOffset - Scale(peekHwnd, 8);
        y = dockRect.top - peekH - margin;
    }

    x = std::max(static_cast<int>(work.left + margin), std::min(static_cast<int>(work.right - peekW - margin), x));
    y = std::max(static_cast<int>(work.top + margin), std::min(static_cast<int>(work.bottom - peekH - margin), y));

    SetWindowPos(peekHwnd, HWND_TOPMOST, x, y, peekW, peekH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
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

HWND GetTargetRichEdit() {
    if (managerHwnd && IsWindowVisible(managerHwnd) && managerNavSection == 0 && managerBodyHwnd && IsWindowVisible(managerBodyHwnd)) {
        return managerBodyHwnd;
    }
    return richEditHwnd;
}

void ApplyFormat(DWORD mask, DWORD effect) {
    HWND target = GetTargetRichEdit();
    if (!target) return;
    CHARFORMAT2W fmt{};
    fmt.cbSize = sizeof(fmt);
    fmt.dwMask = mask;
    SendMessageW(target, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt));
    fmt.dwEffects ^= effect;
    fmt.dwMask = mask;
    SendMessageW(target, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt));
    SetFocus(target);
    if (target == richEditHwnd) {
        SaveEditorContent();
    } else if (auto* sel = GetNoteById(managerSelectedId)) {
        sel->rtf = GetRichEditRtf(managerBodyHwnd);
        sel->body = Narrow(GetWindowTextString(managerBodyHwnd));
        PersistStore();
    }
}

void ApplyFontSizeAndWeight(LONG yHeight, DWORD effects) {
    HWND target = GetTargetRichEdit();
    if (!target) return;
    CHARFORMAT2W fmt{};
    fmt.cbSize = sizeof(fmt);
    fmt.dwMask = CFM_SIZE | CFM_BOLD;
    fmt.yHeight = yHeight;
    fmt.dwEffects = effects;
    SendMessageW(target, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt));
    SetFocus(target);
    if (target == richEditHwnd) {
        SaveEditorContent();
    } else if (auto* sel = GetNoteById(managerSelectedId)) {
        sel->rtf = GetRichEditRtf(managerBodyHwnd);
        sel->body = Narrow(GetWindowTextString(managerBodyHwnd));
        PersistStore();
    }
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
            note.color = (activeColorSwatch + 1) % 8;
            activeColorSwatch = note.color;
            note.archived = false;
            note.title = "Untitled note";
            note.railLabel = "U";
            note.displayTitle = "Untitled note";
            note.isInDeck = true;
            activeId = note.id;
            store.notes.push_back(note);
            PersistStore();
            UpdateDockGeometry();
            SetAppState(AppState::Editing);
            SetFocus(titleBoxHwnd);
            SendMessageW(titleBoxHwnd, EM_SETSEL, 0, -1);
            break;
        }
        case CmdAllNotes: {
            managerNavSection = 0;
            managerFilter = CmdFilterAll;
            RefreshManagerList();
            LoadManagerSelectedNote();
            ShowWindow(managerHwnd, SW_SHOW);
            SetForegroundWindow(managerHwnd);
            if (searchBoxHwnd) SetFocus(searchBoxHwnd);
            break;
        }
        case CmdArchiveView: {
            managerNavSection = 0;
            managerFilter = CmdFilterArchive;
            RefreshManagerList();
            LoadManagerSelectedNote();
            ShowWindow(managerHwnd, SW_SHOW);
            SetForegroundWindow(managerHwnd);
            if (searchBoxHwnd) SetFocus(searchBoxHwnd);
            break;
        }
        case CmdDeleteNote: {
            std::uint64_t targetId = 0;
            if (appState == AppState::Editing && activeId != 0) {
                targetId = activeId;
            } else if (managerSelectedId != 0) {
                targetId = managerSelectedId;
            } else if (activeId != 0) {
                targetId = activeId;
            }
            if (targetId != 0) {
                store.erase(targetId, GetTickCount64());
                if (activeId == targetId) activeId = 0;
                if (managerSelectedId == targetId) managerSelectedId = 0;
                PersistStore();
                UpdateDockGeometry();
                RefreshManagerList();
                if (!managerRows.empty()) {
                    managerSelectedId = managerRows[0];
                }
                if (appState == AppState::Editing) {
                    SetAppState(AppState::Collapsed);
                }
                if (managerHwnd && IsWindowVisible(managerHwnd)) {
                    LoadManagerSelectedNote();
                    InvalidateRect(managerHwnd, nullptr, TRUE);
                }
                ShowUndoToast();
            }
            break;
        }
        case CmdUndoDelete: {
            if (store.deleted && (GetTickCount64() < store.deleted->expires)) {
                auto idToRestore = store.deleted->note.id;
                if (store.undo(GetTickCount64())) {
                    activeId = idToRestore;
                    PersistStore();
                    UpdateDockGeometry();
                    RefreshManagerList();
                    SetAppState(AppState::Editing);
                }
            }
            if (undoHwnd && IsWindowVisible(undoHwnd)) {
                KillTimer(undoHwnd, 1);
                ShowWindow(undoHwnd, SW_HIDE);
            }
            break;
        }
        case CmdCycleColor: {
            if (n) {
                n->color = (n->color + 1) % 8;
                PersistStore();
                LoadActiveNoteIntoEditor();
                if (dockHwnd) InvalidateRect(dockHwnd, nullptr, TRUE);
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
            appearanceTheme = (appearanceTheme == 1 ? 2 : 1);
            config.themeMode = appearanceTheme;
            config.save(configPath);
            InvalidateRect(dockHwnd, nullptr, TRUE);
            if (peekHwnd && IsWindowVisible(peekHwnd)) InvalidateRect(peekHwnd, nullptr, TRUE);
            if (editorHwnd && IsWindowVisible(editorHwnd)) InvalidateRect(editorHwnd, nullptr, TRUE);
            if (managerHwnd && IsWindowVisible(managerHwnd)) InvalidateRect(managerHwnd, nullptr, TRUE);
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
        case CmdUnderlineText: ApplyFormat(CFM_UNDERLINE, CFE_UNDERLINE); break;
        case CmdStrikeText: ApplyFormat(CFM_STRIKEOUT, CFE_STRIKEOUT); break;
        case CmdHeading1: ApplyFontSizeAndWeight(480, CFE_BOLD); break; // 24pt
        case CmdHeading2: ApplyFontSizeAndWeight(380, CFE_BOLD); break; // 19pt
        case CmdHeading3: ApplyFontSizeAndWeight(320, CFE_BOLD); break; // 16pt
        case CmdNormalText: ApplyFontSizeAndWeight(260, 0); break;       // 13pt normal
        case CmdBulletText: {
            HWND target = GetTargetRichEdit();
            PARAFORMAT2 fmt{};
            fmt.cbSize = sizeof(fmt);
            fmt.dwMask = PFM_NUMBERING;
            fmt.wNumbering = PFN_BULLET;
            SendMessageW(target, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&fmt));
            if (target == richEditHwnd) SaveEditorContent();
            else if (auto* sel = GetNoteById(managerSelectedId)) { sel->rtf = GetRichEditRtf(target); sel->body = Narrow(GetWindowTextString(target)); PersistStore(); }
            SetFocus(target);
            break;
        }
        case CmdChecklistText: {
            HWND target = GetTargetRichEdit();
            SendMessageW(target, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\x25A2 "));
            if (target == richEditHwnd) SaveEditorContent();
            else if (auto* sel = GetNoteById(managerSelectedId)) { sel->rtf = GetRichEditRtf(target); sel->body = Narrow(GetWindowTextString(target)); PersistStore(); }
            SetFocus(target);
            break;
        }
        case CmdNumberedList: {
            HWND target = GetTargetRichEdit();
            PARAFORMAT2 fmt{};
            fmt.cbSize = sizeof(fmt);
            fmt.dwMask = PFM_NUMBERING;
            fmt.wNumbering = 2; // PFN_ARABIC
            SendMessageW(target, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&fmt));
            if (target == richEditHwnd) SaveEditorContent();
            else if (auto* sel = GetNoteById(managerSelectedId)) { sel->rtf = GetRichEditRtf(target); sel->body = Narrow(GetWindowTextString(target)); PersistStore(); }
            SetFocus(target);
            break;
        }
        case CmdInsertLink: {
            HWND target = GetTargetRichEdit();
            CHARRANGE sel{};
            SendMessageW(target, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
            if (sel.cpMin != sel.cpMax) {
                ApplyFormat(CFM_LINK | CFM_UNDERLINE, CFE_LINK | CFE_UNDERLINE);
            } else {
                SendMessageW(target, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"https://"));
                if (target == richEditHwnd) SaveEditorContent();
                else if (auto* selN = GetNoteById(managerSelectedId)) { selN->rtf = GetRichEditRtf(target); selN->body = Narrow(GetWindowTextString(target)); PersistStore(); }
            }
            SetFocus(target);
            break;
        }
        case CmdUndoText: {
            HWND target = GetTargetRichEdit();
            SendMessageW(target, EM_UNDO, 0, 0);
            if (target == richEditHwnd) SaveEditorContent();
            else if (auto* sel = GetNoteById(managerSelectedId)) { sel->rtf = GetRichEditRtf(target); sel->body = Narrow(GetWindowTextString(target)); PersistStore(); }
            SetFocus(target);
            break;
        }
        case CmdRedoText: {
            HWND target = GetTargetRichEdit();
            SendMessageW(target, EM_REDO, 0, 0);
            if (target == richEditHwnd) SaveEditorContent();
            else if (auto* sel = GetNoteById(managerSelectedId)) { sel->rtf = GetRichEditRtf(target); sel->body = Narrow(GetWindowTextString(target)); PersistStore(); }
            SetFocus(target);
            break;
        }
        case CmdCodeBlock: {
            HWND target = GetTargetRichEdit();
            CHARFORMAT2W fmt{};
            fmt.cbSize = sizeof(fmt);
            fmt.dwMask = CFM_FACE | CFM_SIZE;
            fmt.yHeight = 220; // 11pt
            lstrcpyW(fmt.szFaceName, L"Consolas");
            SendMessageW(target, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt));
            SendMessageW(target, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"```\r\n// code\r\n```\r\n"));
            if (target == richEditHwnd) SaveEditorContent();
            else if (auto* sel = GetNoteById(managerSelectedId)) { sel->rtf = GetRichEditRtf(target); sel->body = Narrow(GetWindowTextString(target)); PersistStore(); }
            SetFocus(target);
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
        case CmdPopoutNote: {
            if (managerSelectedId != 0) {
                activeId = managerSelectedId;
                ShowWindow(managerHwnd, SW_HIDE);
                SetAppState(AppState::Editing);
            }
            break;
        }
        case CmdMarkComplete: {
            auto* targetNote = (appState == AppState::Editing) ? GetActiveNote() : GetNoteById(managerSelectedId);
            if (targetNote) {
                targetNote->archived = !targetNote->archived;
                PersistStore();
                UpdateDockGeometry();
                RefreshManagerList();
                if (editorHwnd && IsWindowVisible(editorHwnd)) InvalidateRect(editorHwnd, nullptr, TRUE);
                if (managerHwnd && IsWindowVisible(managerHwnd)) InvalidateRect(managerHwnd, nullptr, TRUE);
            }
            break;
        }
        case CmdCloseEditor:
        case CmdMinimiseEditor: {
            SetAppState(AppState::Collapsed);
            break;
        }
        case CmdNavSettings: {
            managerNavSection = 1;
            if (searchBoxHwnd) ShowWindow(searchBoxHwnd, SW_HIDE);
            if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE);
            break;
        }
        case CmdNavNotes: {
            managerNavSection = 0;
            managerFilter = CmdFilterAll;
            RefreshManagerList();
            if (searchBoxHwnd) ShowWindow(searchBoxHwnd, SW_SHOW);
            if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE);
            break;
        }
        case CmdNavDeck: {
            managerNavSection = 0;
            managerFilter = CmdFilterDeck;
            RefreshManagerList();
            if (searchBoxHwnd) ShowWindow(searchBoxHwnd, SW_SHOW);
            if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE);
            break;
        }
        case CmdNavArchive: {
            managerNavSection = 0;
            managerFilter = CmdFilterArchive;
            RefreshManagerList();
            if (searchBoxHwnd) ShowWindow(searchBoxHwnd, SW_SHOW);
            if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE);
            break;
        }
        case CmdNavAbout: {
            managerNavSection = 2;
            if (searchBoxHwnd) ShowWindow(searchBoxHwnd, SW_HIDE);
            if (managerTitleHwnd) ShowWindow(managerTitleHwnd, SW_HIDE);
            if (managerBodyHwnd) ShowWindow(managerBodyHwnd, SW_HIDE);
            if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE);
            break;
        }
        case CmdNavSignIn: {
            managerNavSection = 3;
            if (searchBoxHwnd) ShowWindow(searchBoxHwnd, SW_HIDE);
            if (managerTitleHwnd) ShowWindow(managerTitleHwnd, SW_HIDE);
            if (managerBodyHwnd) ShowWindow(managerBodyHwnd, SW_HIDE);
            if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE);
            break;
        }
        case CmdOpenNotesFolder: {
            std::wstring baseDir;
            if (CheckIsPortableMode()) baseDir = GetExeDir();
            else {
                PWSTR localFolder = nullptr;
                if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localFolder))) {
                    baseDir = std::wstring(localFolder) + L"\\EdgeNotes";
                    CoTaskMemFree(localFolder);
                }
            }
            if (!baseDir.empty()) {
                ShellExecuteW(nullptr, L"open", baseDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            break;
        }
        case CmdThemeAuto: appearanceTheme = 0; if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE); break;
        case CmdThemeLight: appearanceTheme = 1; if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE); break;
        case CmdThemeDark: appearanceTheme = 2; if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE); break;
        case CmdFontHandwritten: noteFontChoice = 0; if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE); break;
        case CmdFontNormal: noteFontChoice = 1; if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE); break;
        case CmdFontRounded: noteFontChoice = 2; if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE); break;
        case CmdFontTypewriter: noteFontChoice = 3; if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE); break;
        case CmdEdgeLeft:
            config.side = DockSide::Left;
            config.save(configPath);
            UpdateDockGeometry();
            if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE);
            break;
        case CmdEdgeRight:
            config.side = DockSide::Right;
            config.save(configPath);
            UpdateDockGeometry();
            if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE);
            break;
        case CmdToggleFullscreenHide:
            hideOverFullscreen = !hideOverFullscreen;
            if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE);
            break;
        case CmdToggleStartWithWindows:
            startWithWindows = !startWithWindows;
            ToggleStartWithWindows();
            if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE);
            break;
        default: {
            if (id >= CmdColorBase && id < CmdColorBase + 8) {
                int c = id - CmdColorBase;
                if (appState == AppState::Editing && n) {
                    n->color = c;
                    PersistStore();
                    LoadActiveNoteIntoEditor();
                } else if (auto* sel = GetNoteById(managerSelectedId)) {
                    sel->color = c;
                    PersistStore();
                    UpdateDockGeometry();
                    if (dockHwnd) InvalidateRect(dockHwnd, nullptr, TRUE);
                    if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE);
                }
            }
            break;
        }
    }
}

void ShowUndoToast() {
    if (!undoHwnd || !store.deleted) return;
    RECT work = GetDockWorkArea();
    int toastW = Scale(undoHwnd, 310);
    int toastH = Scale(undoHwnd, 44);
    int x = work.left + (work.right - work.left - toastW) / 2;
    int y = work.bottom - toastH - Scale(undoHwnd, 28);
    SetWindowPos(undoHwnd, HWND_TOPMOST, x, y, toastW, toastH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetTimer(undoHwnd, 1, 60, nullptr);
    InvalidateRect(undoHwnd, nullptr, TRUE);
}

void PaintUndoToast(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);

    DoubleBuffer buffer;
    buffer.begin(hdc, client.right, client.bottom);
    Gdiplus::Graphics g(buffer.hdcMem);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // Rounded toast pill background
    Gdiplus::RectF toastRect(0.0f, 0.0f, static_cast<float>(client.right), static_cast<float>(client.bottom));
    Gdiplus::GraphicsPath bgPath;
    AddRoundedRectToPath(bgPath, toastRect, static_cast<float>(Scale(hwnd, 8)));
    Gdiplus::SolidBrush bgBrush(ToGdiColor(NightPanel));
    g.FillPath(&bgBrush, &bgPath);
    Gdiplus::Pen borderPen(ToGdiColor(NightBorder), 1.0f);
    g.DrawPath(&borderPen, &bgPath);

    // Draining countdown bar at the bottom edge
    uint64_t now = GetTickCount64();
    float progress = 0.0f;
    if (store.deleted && store.deleted->expires > now) {
        progress = static_cast<float>(store.deleted->expires - now) / 10000.0f;
    }
    progress = Clamp(progress, 0.0f, 1.0f);
    float barW = static_cast<float>(client.right) * progress;
    if (barW > 0.0f) {
        Gdiplus::RectF barRect(0.0f, static_cast<float>(client.bottom - Scale(hwnd, 3)), barW, static_cast<float>(Scale(hwnd, 3)));
        Gdiplus::SolidBrush barBrush(ToGdiColor(PaperColors[0])); // Mint
        g.FillRectangle(&barBrush, barRect);
    }

    // Text on left
    Gdiplus::Font textFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 12)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush textBrush(ToGdiColor(NightInk));
    std::wstring noteDeleted = L"Deleted note";
    if (store.deleted) {
        std::wstring deletedTitle = Wide(store.deleted->note.title.empty() ? "Untitled note" : store.deleted->note.title);
        if (deletedTitle.size() > 16) deletedTitle = deletedTitle.substr(0, 15) + L"\x2026";
        noteDeleted = L"Deleted \x201C" + deletedTitle + L"\x201D";
    }
    g.DrawString(noteDeleted.c_str(), -1, &textFont, Gdiplus::PointF(static_cast<float>(Scale(hwnd, 14)), static_cast<float>(Scale(hwnd, 13))), &textBrush);

    // Undo button on right
    int btnW = Scale(hwnd, 95);
    int btnH = Scale(hwnd, 26);
    Gdiplus::RectF btnRect(static_cast<float>(client.right - btnW - Scale(hwnd, 10)), static_cast<float>(Scale(hwnd, 9)), static_cast<float>(btnW), static_cast<float>(btnH));
    Gdiplus::GraphicsPath btnPath;
    AddRoundedRectToPath(btnPath, btnRect, static_cast<float>(Scale(hwnd, 5)));
    Gdiplus::SolidBrush btnBg(ToGdiColor(NightSurface));
    g.FillPath(&btnBg, &btnPath);
    Gdiplus::Pen btnPen(ToGdiColor(PaperColors[0]), 1.0f);
    g.DrawPath(&btnPen, &btnPath);

    Gdiplus::Font btnFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 11)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush btnText(ToGdiColor(PaperColors[0]));
    Gdiplus::StringFormat csf;
    csf.SetAlignment(Gdiplus::StringAlignmentCenter);
    csf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    g.DrawString(L"Undo (Ctrl+Z)", -1, &btnFont, btnRect, &csf, &btnText);

    buffer.end(hdc);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK UndoWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            PaintUndoToast(hwnd);
            return 0;

        case WM_TIMER: {
            uint64_t now = GetTickCount64();
            if (!store.deleted || now >= store.deleted->expires) {
                KillTimer(hwnd, 1);
                ShowWindow(hwnd, SW_HIDE);
                store.deleted.reset();
            } else {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            int x = GET_X_LPARAM(lp);
            RECT client;
            GetClientRect(hwnd, &client);
            int btnW = Scale(hwnd, 95);
            if (x >= client.right - btnW - Scale(hwnd, 10)) {
                KillTimer(hwnd, 1);
                ShowWindow(hwnd, SW_HIDE);
                ExecuteCommand(CmdUndoDelete);
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (wp == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                KillTimer(hwnd, 1);
                ShowWindow(hwnd, SW_HIDE);
                ExecuteCommand(CmdUndoDelete);
                return 0;
            }
            if (wp == VK_ESCAPE) {
                KillTimer(hwnd, 1);
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Subclass procedure for titleBox and richEdit controls
LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_LBUTTONUP && hwnd == richEditHwnd) {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        POINTL ptl{pt.x, pt.y};
        int charIdx = static_cast<int>(SendMessageW(hwnd, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&ptl)));
        if (charIdx >= 0) {
            wchar_t ch[2]{};
            TEXTRANGEW tr{{charIdx, charIdx + 1}, ch};
            SendMessageW(hwnd, EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&tr));
            if (ch[0] == 0x2610) { // ☐ -> ☑
                CHARRANGE sel{charIdx, charIdx + 1};
                SendMessageW(hwnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&sel));
                SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\x2611"));
                SaveEditorContent();
                return 0;
            } else if (ch[0] == 0x2611) { // ☑ -> ☐
                CHARRANGE sel{charIdx, charIdx + 1};
                SendMessageW(hwnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&sel));
                SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\x2610"));
                SaveEditorContent();
                return 0;
            }
        }
    }

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

    // Auto-formatting triggers: "# ", "## ", "### ", "- ", "* ", "1. ", "[] ", "[ ] ", "[x] ", "[X] "
    if (msg == WM_CHAR && wp == ' ' && hwnd == richEditHwnd) {
        CHARRANGE selection{};
        SendMessageW(hwnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
        LONG line = static_cast<LONG>(SendMessageW(hwnd, EM_LINEFROMCHAR, selection.cpMin, 0));
        LONG start = static_cast<LONG>(SendMessageW(hwnd, EM_LINEINDEX, line, 0));
        LONG len = selection.cpMin - start;
        if (selection.cpMin == selection.cpMax && len > 0 && len <= 4) {
            wchar_t buf[16]{};
            TEXTRANGEW range{{start, selection.cpMin}, buf};
            SendMessageW(hwnd, EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&range));
            std::wstring trigger(buf);
            if (trigger == L"#" || trigger == L"##" || trigger == L"###" ||
                trigger == L"-" || trigger == L"*" || trigger == L"1." ||
                trigger == L"[]" || trigger == L"[ ]" || trigger == L"[x]" || trigger == L"[X]") {
                lastAutoFormatPrefix = trigger;
                lastAutoFormatLine = line;
                isLoadingEditor = true;
                CHARRANGE removeRange{start, selection.cpMin};
                SendMessageW(hwnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&removeRange));
                SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
                isLoadingEditor = false;

                if (trigger == L"-" || trigger == L"*") {
                    ExecuteCommand(CmdBulletText);
                } else if (trigger == L"1.") {
                    ExecuteCommand(CmdNumberedList);
                } else if (trigger == L"[]" || trigger == L"[ ]") {
                    SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\x2610 "));
                    SaveEditorContent();
                } else if (trigger == L"[x]" || trigger == L"[X]") {
                    SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\x2611 "));
                    SaveEditorContent();
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

    // 1. Drag Grip at the top/start (3x3 circular dots with hover highlight)
    if (hoveredTabIndex == -2) {
        Gdiplus::RectF gripHovR(
            isVertical ? static_cast<float>(Scale(hwnd, 4)) : static_cast<float>(Scale(hwnd, 2)),
            isVertical ? static_cast<float>(Scale(hwnd, 2)) : static_cast<float>(Scale(hwnd, 4)),
            isVertical ? static_cast<float>(client.right - Scale(hwnd, 8)) : static_cast<float>(gripSize),
            isVertical ? static_cast<float>(gripSize) : static_cast<float>(client.bottom - Scale(hwnd, 8))
        );
        Gdiplus::GraphicsPath gripPath;
        AddRoundedRectToPath(gripPath, gripHovR, static_cast<float>(Scale(hwnd, 6)));
        Gdiplus::SolidBrush gripHovB(Gdiplus::Color(45, 255, 255, 255));
        g.FillPath(&gripHovB, &gripPath);
    }
    Gdiplus::SolidBrush gripBrush(hoveredTabIndex == -2 ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(210, 255, 255, 255));
    int gripDotsX = isVertical ? (client.right / 2 - Scale(hwnd, 5)) : Scale(hwnd, 10);
    int gripDotsY = isVertical ? Scale(hwnd, 8) : (client.bottom / 2 - Scale(hwnd, 5));
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            g.FillEllipse(&gripBrush, gripDotsX + c * Scale(hwnd, 5), gripDotsY + r * Scale(hwnd, 5), Scale(hwnd, 2), Scale(hwnd, 2));
        }
    }

    int currentPos = gripSize + gap;

    // 2. Active Note Tabs (Pill tabs displaying effectiveRailLabel in pastel note colors)
    auto deckNotes = GetDeckNotes();
    if (!deckNotes.empty()) {
        for (std::size_t i = 0; i < deckNotes.size(); ++i) {
            const auto* note = deckNotes[i];
            bool isHovered = (hoveredTabIndex == static_cast<int>(i));
            bool isActive = (activeId == note->id && (appState == AppState::Editing || appState == AppState::Peek));

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

            COLORREF colorVal = PaperColors[note->color % 8];
            Gdiplus::SolidBrush tabBrush(ToGdiColor(colorVal));
            g.FillPath(&tabBrush, &path);

            Gdiplus::Pen borderPen(Gdiplus::Color(45, 0, 0, 0), 1.0f);
            g.DrawPath(&borderPen, &path);

            std::wstring labelText = Wide(note->effectiveRailLabel());
            Gdiplus::SolidBrush textBrush(ToGdiColor(PaperInk));
            Gdiplus::Font font(L"Segoe UI", static_cast<float>(Scale(hwnd, 12)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

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
                g.DrawString(labelText.c_str(), -1, &font, textBound, &sf, &textBrush);
                g.Restore(gState);
            } else {
                g.DrawString(labelText.c_str(), -1, &font, tabRect, &sf, &textBrush);
            }

            currentPos += pillLen + gap;
        }
        currentPos += Scale(hwnd, 10);
    } else {
        currentPos += Scale(hwnd, 6);
    }

    // 3. Circular icon buttons below tabs: [+] [☀/🌙] [🎛]
    auto drawCircleButton = [&](int btnIndex, bool isWhiteBtn, auto drawIcon) {
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

        Gdiplus::Color bgColor;
        Gdiplus::Color borderColor;
        if (isWhiteBtn) {
            bgColor = isHovered ? Gdiplus::Color(255, 235, 235, 240) : Gdiplus::Color(255, 255, 255, 255);
            borderColor = Gdiplus::Color(80, 0, 0, 0);
        } else {
            bgColor = isHovered ? Gdiplus::Color(255, 45, 48, 58) : Gdiplus::Color(255, 24, 26, 32);
            borderColor = Gdiplus::Color(60, 255, 255, 255);
        }
        Gdiplus::SolidBrush bgBrush(bgColor);
        g.FillPath(&bgBrush, &btnPath);

        Gdiplus::Pen borderPen(borderColor, 1.0f);
        g.DrawPath(&borderPen, &btnPath);

        drawIcon(btnRect, isHovered);
        currentPos += iconBtnSize + iconGap;
    };

    // Button 100: [+] (New note - white circular button matching Screenshot 3)
    drawCircleButton(100, true, [&](Gdiplus::RectF r, bool) {
        Gdiplus::Font plusFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 17)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush plusText(Gdiplus::Color(255, 20, 22, 28));
        Gdiplus::StringFormat csf;
        csf.SetAlignment(Gdiplus::StringAlignmentCenter);
        csf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(L"+", -1, &plusFont, r, &csf, &plusText);
    });

    // Button 101: [☀ / 🌙] (Theme toggle)
    drawCircleButton(101, false, [&](Gdiplus::RectF r, bool) {
        Gdiplus::Font themeFont(L"Segoe UI Symbol", static_cast<float>(Scale(hwnd, 13)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush themeText(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::StringFormat csf;
        csf.SetAlignment(Gdiplus::StringAlignmentCenter);
        csf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        const wchar_t* symbol = (appearanceTheme == 1) ? L"\x2600" : L"\x263E"; // ☀ or ☾
        g.DrawString(symbol, -1, &themeFont, r, &csf, &themeText);
    });

    // Button 102: [📌] (Pin button - keeps dock & editor open)
    drawCircleButton(102, false, [&](Gdiplus::RectF r, bool) {
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
    drawCircleButton(103, false, [&](Gdiplus::RectF r, bool) {
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

    auto deckNotes = GetDeckNotes();
    int tabCount = static_cast<int>(deckNotes.size());
    int current = gripSize + gap;
    for (int i = 0; i < tabCount; ++i) {
        if (pos >= current && pos < current + pillLen) {
            return i; // Note tab index
        }
        current += pillLen + gap;
    }

    if (tabCount > 0) {
        current += Scale(hwnd, 10);
    } else {
        current += Scale(hwnd, 6);
    }

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
            if (hit == -2) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                return TRUE;
            }
            if ((hit >= 0 && hit < 100) || (hit >= 100 && hit <= 103)) {
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
                if (hit >= 0 && hit < 100 && appState == AppState::Collapsed) {
                    auto deckNotes = GetDeckNotes();
                    if (!deckNotes.empty() && hit < static_cast<int>(deckNotes.size())) {
                        activeId = deckNotes[hit]->id;
                    }
                    SetAppState(AppState::Peek);
                }
            }

            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE: {
            if (!isDraggingDock) {
                hoveredTabIndex = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
                if (!config.pinned && appState == AppState::Peek) {
                    POINT pt;
                    GetCursorPos(&pt);
                    RECT dockR{}, peekR{};
                    GetWindowRect(dockHwnd, &dockR);
                    GetWindowRect(peekHwnd, &peekR);
                    if (!PtInRect(&dockR, pt) && !PtInRect(&peekR, pt)) {
                        SetAppState(AppState::Collapsed);
                    }
                }
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

            auto deckNotes = GetDeckNotes();
            if (hit >= 0 && hit < 100) {
                if (deckNotes.empty()) {
                    ExecuteCommand(CmdNewNote);
                } else if (hit < static_cast<int>(deckNotes.size())) {
                    activeId = deckNotes[hit]->id;
                    SetAppState(AppState::Editing);
                    LoadActiveNoteIntoEditor();
                }
                InvalidateRect(dockHwnd, nullptr, TRUE);
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

    auto deckNotes = GetDeckNotes();
    int tabIdx = 0;
    if (hoveredTabIndex >= 0 && hoveredTabIndex < static_cast<int>(deckNotes.size())) {
        tabIdx = hoveredTabIndex;
    } else {
        for (std::size_t i = 0; i < deckNotes.size(); ++i) {
            if (deckNotes[i]->id == activeId) {
                tabIdx = static_cast<int>(i);
                break;
            }
        }
    }

    const Note* note = (!deckNotes.empty() && tabIdx < static_cast<int>(deckNotes.size())) ? deckNotes[tabIdx] : nullptr;

    COLORREF paperColor = note ? PaperColors[note->color % 8] : PaperColors[0];
    COLORREF inkColor = note ? PaperInks[note->color % 8] : PaperInk;

    // Draw full pastel card with smooth rounded corners matching hero-deck.mp4
    Gdiplus::RectF cardRect(0.5f, 0.5f, static_cast<float>(client.right - 1), static_cast<float>(client.bottom - 1));
    Gdiplus::GraphicsPath cardPath;
    AddRoundedRectToPath(cardPath, cardRect, ScaleF(hwnd, 12.0f));

    Gdiplus::SolidBrush bgBrush(ToGdiColor(paperColor));
    g.FillPath(&bgBrush, &cardPath);

    Gdiplus::Pen borderPen(ToGdiColor(inkColor, 50), 1.0f);
    g.DrawPath(&borderPen, &cardPath);

    // Vertical seam label on the dock-facing side (matching hero-deck.mp4)
    bool isDockRight = (config.side == DockSide::Right);
    std::wstring seamLabel = note ? Wide(note->effectiveRailLabel()) : L"NEW";

    int seamW = Scale(hwnd, 20);
    int pad = Scale(hwnd, 14);

    Gdiplus::Font seamFont(L"Segoe UI", ScaleF(hwnd, 9.5f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush seamBrush(ToGdiColor(inkColor, 120));
    Gdiplus::StringFormat seamSf;
    seamSf.SetAlignment(Gdiplus::StringAlignmentCenter);
    seamSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    if (isDockRight) {
        Gdiplus::GraphicsState sState = g.Save();
        float cx = static_cast<float>(client.right - seamW / 2);
        float cy = static_cast<float>(client.bottom / 2);
        g.TranslateTransform(cx, cy);
        g.RotateTransform(90.0f);
        Gdiplus::RectF seamBound(-static_cast<float>(client.bottom / 2), -static_cast<float>(seamW / 2), static_cast<float>(client.bottom), static_cast<float>(seamW));
        g.DrawString(seamLabel.c_str(), -1, &seamFont, seamBound, &seamSf, &seamBrush);
        g.Restore(sState);
    } else {
        Gdiplus::GraphicsState sState = g.Save();
        float cx = static_cast<float>(seamW / 2);
        float cy = static_cast<float>(client.bottom / 2);
        g.TranslateTransform(cx, cy);
        g.RotateTransform(270.0f);
        Gdiplus::RectF seamBound(-static_cast<float>(client.bottom / 2), -static_cast<float>(seamW / 2), static_cast<float>(client.bottom), static_cast<float>(seamW));
        g.DrawString(seamLabel.c_str(), -1, &seamFont, seamBound, &seamSf, &seamBrush);
        g.Restore(sState);
    }

    // Inside card: Title, timestamp, and handwritten body text
    float contentLeft = isDockRight ? static_cast<float>(pad) : static_cast<float>(pad + seamW);
    float contentRight = isDockRight ? static_cast<float>(client.right - pad - seamW) : static_cast<float>(client.right - pad);
    float contentW = contentRight - contentLeft;

    std::wstring titleText = note ? Wide(note->title.empty() ? "Untitled note" : note->title) : L"+ A fresh page";
    Gdiplus::Font titleFontD(L"Segoe UI", ScaleF(hwnd, 14.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush inkBrush(ToGdiColor(inkColor));
    g.DrawString(titleText.c_str(), -1, &titleFontD, Gdiplus::PointF(contentLeft, static_cast<float>(pad)), &inkBrush);

    Gdiplus::Font timeFont(L"Segoe UI", ScaleF(hwnd, 9.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush timeBrush(ToGdiColor(inkColor, 140));
    Gdiplus::StringFormat rightSf;
    rightSf.SetAlignment(Gdiplus::StringAlignmentFar);
    Gdiplus::RectF timeRect(contentLeft, static_cast<float>(pad + 2), contentW, static_cast<float>(Scale(hwnd, 18)));
    g.DrawString(note ? L"Click to open \x2197" : L"Click to write", -1, &timeFont, timeRect, &rightSf, &timeBrush);

    // Body in authentic Excalifont handwriting!
    std::wstring bodySnippet = note ? Wide(note->body.substr(0, 160)) : L"Click this card to create your first note or thought.";
    if (note && note->body.empty()) {
        bodySnippet = L"Empty note. Click to start writing...";
    }
    Gdiplus::Font handF(L"Excalifont", ScaleF(hwnd, 13.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::RectF bodyRect(contentLeft, static_cast<float>(pad + Scale(hwnd, 26)), contentW, static_cast<float>(client.bottom - pad * 2 - Scale(hwnd, 26)));
    Gdiplus::StringFormat bodySf;
    bodySf.SetTrimming(Gdiplus::StringTrimmingEllipsisWord);
    g.DrawString(bodySnippet.c_str(), -1, &handF, bodyRect, &bodySf, &inkBrush);

    buffer.end(hdc);
    EndPaint(hwnd, &ps);
}

int HitTestPeek(HWND /*hwnd*/, int /*x*/, int /*y*/) {
    return 0; // Card clicked
}

LRESULT CALLBACK PeekWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:
            PaintPeek(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;

        case WM_MOUSELEAVE: {
            if (!config.pinned && appState == AppState::Peek) {
                POINT pt;
                GetCursorPos(&pt);
                RECT dockR{}, peekR{};
                GetWindowRect(dockHwnd, &dockR);
                GetWindowRect(peekHwnd, &peekR);
                if (!PtInRect(&dockR, pt) && !PtInRect(&peekR, pt)) {
                    SetAppState(AppState::Collapsed);
                }
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            auto deckNotes = GetDeckNotes();
            int tabIdx = (hoveredTabIndex >= 0 && hoveredTabIndex < static_cast<int>(deckNotes.size())) ? hoveredTabIndex : 0;
            if (!deckNotes.empty() && tabIdx < static_cast<int>(deckNotes.size())) {
                activeId = deckNotes[tabIdx]->id;
                SetAppState(AppState::Editing);
            } else {
                ExecuteCommand(CmdNewNote);
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
    if (!titleBoxHwnd || !richEditHwnd) return;
    if (statusBoxHwnd) ShowWindow(statusBoxHwnd, SW_HIDE);
    RECT client;
    GetClientRect(hwnd, &client);

    int pad = Scale(hwnd, 20);
    int topY = Scale(hwnd, 14);
    int titleH = Scale(hwnd, 32);
    int titleW = client.right - pad - Scale(hwnd, 180);

    int richY = topY + titleH + Scale(hwnd, 8);
    int toolbarH = Scale(hwnd, 72);
    int richH = client.bottom - richY - toolbarH - Scale(hwnd, 6);
    richH = std::max(Scale(hwnd, 50), richH);

    MoveWindow(titleBoxHwnd, pad, topY, titleW, titleH, TRUE);
    MoveWindow(richEditHwnd, pad, richY, client.right - pad * 2, richH, TRUE);
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
    COLORREF inkColor = n ? PaperInks[n->color % 8] : PaperInk;

    // Fill entire sheet in pastel paper color
    Gdiplus::SolidBrush bgBrush(ToGdiColor(paperColor));
    g.FillRectangle(&bgBrush, 0, 0, client.right, client.bottom);

    int pad = Scale(hwnd, 20);
    Gdiplus::SolidBrush inkBrush(ToGdiColor(inkColor));
    Gdiplus::SolidBrush mutedBrush(ToGdiColor(inkColor, 150));
    Gdiplus::StringFormat centerSf;
    centerSf.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    // 1. Header: Top right Saved · 20m, Minimize (—), Close (✕)
    int btnSize = Scale(hwnd, 24);
    int btnY = Scale(hwnd, 16);
    int closeX = client.right - pad - btnSize;
    int minX = closeX - btnSize - Scale(hwnd, 4);

    Gdiplus::Font headerFont(L"Segoe UI", ScaleF(hwnd, 10.5f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font btnFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 12)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

    // Saved · 20m
    Gdiplus::RectF savedRect(static_cast<float>(minX - Scale(hwnd, 110)), static_cast<float>(btnY), static_cast<float>(Scale(hwnd, 104)), static_cast<float>(btnSize));
    Gdiplus::StringFormat rightSf;
    rightSf.SetAlignment(Gdiplus::StringAlignmentFar);
    rightSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    g.DrawString(L"Saved \x00B7 20m", -1, &headerFont, savedRect, &rightSf, &mutedBrush);

    // Minimize [—]
    Gdiplus::RectF minRect(static_cast<float>(minX), static_cast<float>(btnY), static_cast<float>(btnSize), static_cast<float>(btnSize));
    if (hoveredToolbarBtn == CmdMinimiseEditor) {
        Gdiplus::GraphicsPath mp;
        AddRoundedRectToPath(mp, minRect, static_cast<float>(Scale(hwnd, 4)));
        Gdiplus::SolidBrush hovB(ToGdiColor(inkColor, 35));
        g.FillPath(&hovB, &mp);
    }
    g.DrawString(L"\x2014", -1, &btnFont, minRect, &centerSf, &inkBrush);

    // Close [✕]
    Gdiplus::RectF closeRect(static_cast<float>(closeX), static_cast<float>(btnY), static_cast<float>(btnSize), static_cast<float>(btnSize));
    if (hoveredToolbarBtn == CmdCloseEditor || hoveredToolbarBtn == CmdExitApp) {
        Gdiplus::GraphicsPath cp;
        AddRoundedRectToPath(cp, closeRect, static_cast<float>(Scale(hwnd, 4)));
        Gdiplus::SolidBrush hovB(ToGdiColor(inkColor, 35));
        g.FillPath(&hovB, &cp);
    }
    g.DrawString(L"\x2715", -1, &btnFont, closeRect, &centerSf, &inkBrush);

    // 2. Bottom Toolbar: Row 1 Formatting, Row 2 Colors + Actions
    int row1Y = client.bottom - Scale(hwnd, 68);
    int row1H = Scale(hwnd, 26);
    int row2Y = client.bottom - Scale(hwnd, 36);
    int row2H = Scale(hwnd, 26);

    // Row 1: Formatting icons
    struct FormatBtn { int id; const wchar_t* label; };
    FormatBtn fbtns[] = {
        {CmdBoldText, L"B"},
        {CmdItalicText, L"I"},
        {CmdUnderlineText, L"U"},
        {CmdStrikeText, L"S"},
        {0, L"|"},
        {CmdChecklistText, L"\x2611"},
        {CmdBulletText, L":="},
        {CmdNumberedList, L"1="},
        {0, L"|"},
        {CmdInsertLink, L"\xD83D\xDD17"},
        {CmdNormalText, L"Aa"},
        {CmdMoreMenu, L"\x2026"}
    };

    int fBtnX = pad;
    Gdiplus::Font fmtFont(L"Segoe UI", static_cast<float>(Scale(hwnd, 11)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Pen divPen(ToGdiColor(inkColor, 40), 1.0f);

    for (const auto& fb : fbtns) {
        if (fb.id == 0) {
            g.DrawLine(&divPen, fBtnX + Scale(hwnd, 5), row1Y + Scale(hwnd, 4), fBtnX + Scale(hwnd, 5), row1Y + row1H - Scale(hwnd, 4));
            fBtnX += Scale(hwnd, 12);
            continue;
        }
        int btnW = Scale(hwnd, 24);
        Gdiplus::RectF br(static_cast<float>(fBtnX), static_cast<float>(row1Y), static_cast<float>(btnW), static_cast<float>(row1H));
        if (hoveredToolbarBtn == fb.id) {
            Gdiplus::GraphicsPath bp;
            AddRoundedRectToPath(bp, br, static_cast<float>(Scale(hwnd, 4)));
            Gdiplus::SolidBrush hovB(ToGdiColor(inkColor, 35));
            g.FillPath(&hovB, &bp);
        }
        g.DrawString(fb.label, -1, &fmtFont, br, &centerSf, &inkBrush);
        fBtnX += btnW + Scale(hwnd, 3);
    }

    // Row 2: Left 8 color swatches + Right actions
    int swX = pad;
    int swSize = Scale(hwnd, 14);
    int swY = row2Y + (row2H - swSize) / 2;

    for (int c = 0; c < 8; ++c) {
        Gdiplus::RectF sr(static_cast<float>(swX), static_cast<float>(swY), static_cast<float>(swSize), static_cast<float>(swSize));
        Gdiplus::SolidBrush sBrush(ToGdiColor(PaperColors[c]));
        g.FillEllipse(&sBrush, sr);
        if (n && (n->color % 8) == static_cast<unsigned>(c)) {
            // Distinct focus ring
            Gdiplus::Pen selPen(ToGdiColor(inkColor), 2.0f);
            Gdiplus::RectF ring(sr.X - 2.5f, sr.Y - 2.5f, sr.Width + 5.0f, sr.Height + 5.0f);
            g.DrawEllipse(&selPen, ring);
        } else if (hoveredToolbarBtn == CmdColorBase + c) {
            Gdiplus::Pen hovPen(ToGdiColor(inkColor, 120), 1.5f);
            g.DrawEllipse(&hovPen, sr);
        }
        swX += swSize + Scale(hwnd, 8);
    }

    // Right actions: Delete (red), Mark complete, Close
    int actRight = client.right - pad;
    Gdiplus::Font actFont(L"Segoe UI", ScaleF(hwnd, 10.5f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

    // Close
    int closeTxtW = Scale(hwnd, 44);
    actRight -= closeTxtW;
    Gdiplus::RectF cr(static_cast<float>(actRight), static_cast<float>(row2Y), static_cast<float>(closeTxtW), static_cast<float>(row2H));
    if (hoveredToolbarBtn == CmdCloseEditor) {
        Gdiplus::GraphicsPath cp;
        AddRoundedRectToPath(cp, cr, static_cast<float>(Scale(hwnd, 4)));
        Gdiplus::SolidBrush hovB(ToGdiColor(inkColor, 25));
        g.FillPath(&hovB, &cp);
    }
    g.DrawString(L"Close", -1, &actFont, cr, &centerSf, &inkBrush);

    // Mark complete
    int markTxtW = Scale(hwnd, 96);
    actRight -= (markTxtW + Scale(hwnd, 6));
    Gdiplus::RectF mr(static_cast<float>(actRight), static_cast<float>(row2Y), static_cast<float>(markTxtW), static_cast<float>(row2H));
    if (hoveredToolbarBtn == CmdMarkComplete) {
        Gdiplus::GraphicsPath mp;
        AddRoundedRectToPath(mp, mr, static_cast<float>(Scale(hwnd, 4)));
        Gdiplus::SolidBrush hovB(ToGdiColor(inkColor, 25));
        g.FillPath(&hovB, &mp);
    }
    std::wstring markText = (n && n->archived) ? L"Restore note" : L"Mark complete";
    g.DrawString(markText.c_str(), -1, &actFont, mr, &centerSf, &inkBrush);

    // Delete (Red text)
    int delTxtW = Scale(hwnd, 52);
    actRight -= (delTxtW + Scale(hwnd, 6));
    Gdiplus::RectF dr(static_cast<float>(actRight), static_cast<float>(row2Y), static_cast<float>(delTxtW), static_cast<float>(row2H));
    if (hoveredToolbarBtn == CmdDeleteNote) {
        Gdiplus::GraphicsPath dp;
        AddRoundedRectToPath(dp, dr, static_cast<float>(Scale(hwnd, 4)));
        Gdiplus::SolidBrush hovB(Gdiplus::Color(35, 239, 68, 68));
        g.FillPath(&hovB, &dp);
    }
    Gdiplus::SolidBrush redBrush(Gdiplus::Color(255, 220, 38, 38));
    g.DrawString(L"Delete", -1, &actFont, dr, &centerSf, &redBrush);

    // 3. Hover Tooltip Badge
    if (hoveredToolbarBtn != 0) {
        std::wstring tip;
        switch (hoveredToolbarBtn) {
            case CmdBoldText: tip = L"Bold (Ctrl+B)"; break;
            case CmdItalicText: tip = L"Italic (Ctrl+I)"; break;
            case CmdUnderlineText: tip = L"Underline (Ctrl+U)"; break;
            case CmdStrikeText: tip = L"Strikethrough (Ctrl+Shift+S)"; break;
            case CmdChecklistText: tip = L"Checklist (Ctrl+Shift+L)"; break;
            case CmdBulletText: tip = L"Bullet list (Ctrl+Shift+8)"; break;
            case CmdNumberedList: tip = L"Numbered list"; break;
            case CmdInsertLink: tip = L"Insert link (Ctrl+K)"; break;
            case CmdNormalText: tip = L"Text formatting"; break;
            case CmdMoreMenu: tip = L"More options"; break;
            case CmdMinimiseEditor: tip = L"Minimize (Esc)"; break;
            case CmdCloseEditor: tip = L"Close note"; break;
            case CmdDeleteNote: tip = L"Delete note (Ctrl+BkSp)"; break;
            case CmdMarkComplete: tip = (n && n->archived) ? L"Restore note" : L"Archive note"; break;
            case CmdColorBase + 0: tip = L"Mint"; break;
            case CmdColorBase + 1: tip = L"Sky Blue"; break;
            case CmdColorBase + 2: tip = L"Lavender"; break;
            case CmdColorBase + 3: tip = L"Lemon"; break;
            case CmdColorBase + 4: tip = L"Rose"; break;
            case CmdColorBase + 5: tip = L"Peach"; break;
            case CmdColorBase + 6: tip = L"Sage"; break;
            case CmdColorBase + 7: tip = L"Ice"; break;
            default: break;
        }

        if (!tip.empty()) {
            Gdiplus::Font tipFont(L"Segoe UI", ScaleF(hwnd, 9.5f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::RectF bound;
            g.MeasureString(tip.c_str(), -1, &tipFont, Gdiplus::PointF(0, 0), &bound);

            int tipW = static_cast<int>(bound.Width) + Scale(hwnd, 14);
            int tipH = Scale(hwnd, 20);

            POINT curPt;
            GetCursorPos(&curPt);
            ScreenToClient(hwnd, &curPt);
            int tipX = curPt.x - tipW / 2;
            int tipY = curPt.y - tipH - Scale(hwnd, 8);
            if (tipX < Scale(hwnd, 8)) tipX = Scale(hwnd, 8);
            if (tipX + tipW > client.right - Scale(hwnd, 8)) tipX = client.right - Scale(hwnd, 8) - tipW;
            if (tipY < Scale(hwnd, 4)) tipY = curPt.y + Scale(hwnd, 16);

            Gdiplus::RectF tipRect(static_cast<float>(tipX), static_cast<float>(tipY), static_cast<float>(tipW), static_cast<float>(tipH));
            Gdiplus::GraphicsPath tp;
            AddRoundedRectToPath(tp, tipRect, ScaleF(hwnd, 4.0f));
            Gdiplus::SolidBrush tipBg(Gdiplus::Color(240, 24, 25, 29));
            g.FillPath(&tipBg, &tp);
            Gdiplus::Pen tipPen(Gdiplus::Color(100, 255, 255, 255), 1.0f);
            g.DrawPath(&tipPen, &tp);

            Gdiplus::SolidBrush tipTextBrush(Gdiplus::Color(255, 255, 255, 255));
            g.DrawString(tip.c_str(), -1, &tipFont, tipRect, &centerSf, &tipTextBrush);
        }
    }

    buffer.end(hdc);
    EndPaint(hwnd, &ps);
}

int HitTestEditor(HWND hwnd, int x, int y) {
    RECT client;
    GetClientRect(hwnd, &client);
    int pad = Scale(hwnd, 20);
    int btnSize = Scale(hwnd, 24);
    int btnY = Scale(hwnd, 16);
    int closeX = client.right - pad - btnSize;
    int minX = closeX - btnSize - Scale(hwnd, 4);

    // Minimize [—]
    if (x >= minX && x <= minX + btnSize && y >= btnY && y <= btnY + btnSize) return CmdMinimiseEditor;
    // Close [✕]
    if (x >= closeX && x <= closeX + btnSize && y >= btnY && y <= btnY + btnSize) return CmdCloseEditor;

    int row1Y = client.bottom - Scale(hwnd, 68);
    int row1H = Scale(hwnd, 26);
    int row2Y = client.bottom - Scale(hwnd, 36);
    int row2H = Scale(hwnd, 26);

    // Row 1: Formatting icons
    if (y >= row1Y && y <= row1Y + row1H) {
        int fBtnX = pad;
        int tools[] = {
            CmdBoldText, CmdItalicText, CmdUnderlineText, CmdStrikeText,
            0,
            CmdChecklistText, CmdBulletText, CmdNumberedList,
            0,
            CmdInsertLink, CmdNormalText, CmdMoreMenu
        };
        for (int cmd : tools) {
            if (cmd == 0) {
                fBtnX += Scale(hwnd, 12);
                continue;
            }
            int btnW = Scale(hwnd, 24);
            if (x >= fBtnX && x <= fBtnX + btnW) return cmd;
            fBtnX += btnW + Scale(hwnd, 3);
        }
    }

    // Row 2: Swatches and actions
    if (y >= row2Y && y <= row2Y + row2H) {
        // Swatches
        int swX = pad;
        int swSize = Scale(hwnd, 14);
        for (int c = 0; c < 8; ++c) {
            if (x >= swX - 3 && x <= swX + swSize + 3) {
                return CmdColorBase + c;
            }
            swX += swSize + Scale(hwnd, 8);
        }

        // Right actions: Delete, Mark complete, Close
        int actRight = client.right - pad;
        int closeTxtW = Scale(hwnd, 44);
        actRight -= closeTxtW;
        if (x >= actRight && x <= actRight + closeTxtW) return CmdCloseEditor;

        int markTxtW = Scale(hwnd, 96);
        actRight -= (markTxtW + Scale(hwnd, 6));
        if (x >= actRight && x <= actRight + markTxtW) return CmdMarkComplete;

        int delTxtW = Scale(hwnd, 52);
        actRight -= (delTxtW + Scale(hwnd, 6));
        if (x >= actRight && x <= actRight + delTxtW) return CmdDeleteNote;
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

    if (managerHwnd) {
        LoadManagerSelectedNote();
        InvalidateRect(managerHwnd, nullptr, TRUE);
    }
}

void LayoutManagerControls(HWND hwnd) {
    if (!hwnd) return;
    RECT client;
    GetClientRect(hwnd, &client);
    int pad = Scale(hwnd, 12);
    int bannerH = Scale(hwnd, 56);
    int col1W = Scale(hwnd, 190);
    int bodyY = bannerH;
    int bodyH = client.bottom - bodyY;

    if (managerNavSection == 0) {
        int col2W = Scale(hwnd, 320);
        int col2X = col1W;

        // Position search box in Column 2
        int sbY = bodyY + Scale(hwnd, 14);
        int sbW = col2W - Scale(hwnd, 90);
        int sbH = Scale(hwnd, 28);
        if (searchBoxHwnd) {
            SetWindowPos(searchBoxHwnd, nullptr, col2X + pad, sbY, sbW, sbH, SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(searchBoxHwnd, SW_SHOW);
        }

        // Column 3: Sheet Editor
        auto* selNote = GetNoteById(managerSelectedId);
        if (selNote && managerTitleHwnd && managerBodyHwnd) {
            int col3X = col1W + col2W;
            int col3W = client.right - col3X;
            int prevPad = Scale(hwnd, 24);
            int statY = bodyY + Scale(hwnd, 16);
            int sheetY = statY + Scale(hwnd, 28);
            int sheetW = col3W - prevPad * 2;
            int sheetH = bodyH - Scale(hwnd, 72);
            int sheetX = col3X + prevPad;

            int titleX = sheetX + Scale(hwnd, 20);
            int titleY = sheetY + Scale(hwnd, 16);
            int titleW = sheetW - Scale(hwnd, 140);
            int titleH = Scale(hwnd, 28);

            int richX = sheetX + Scale(hwnd, 20);
            int richY = sheetY + Scale(hwnd, 50);
            int richW = sheetW - Scale(hwnd, 40);
            int richH = sheetH - Scale(hwnd, 120);
            richH = std::max(Scale(hwnd, 40), richH);

            SetWindowPos(managerTitleHwnd, nullptr, titleX, titleY, titleW, titleH, SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(managerBodyHwnd, nullptr, richX, richY, richW, richH, SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(managerTitleHwnd, SW_SHOW);
            ShowWindow(managerBodyHwnd, SW_SHOW);
        } else {
            if (managerTitleHwnd) ShowWindow(managerTitleHwnd, SW_HIDE);
            if (managerBodyHwnd) ShowWindow(managerBodyHwnd, SW_HIDE);
        }
    } else {
        if (searchBoxHwnd) ShowWindow(searchBoxHwnd, SW_HIDE);
        if (managerTitleHwnd) ShowWindow(managerTitleHwnd, SW_HIDE);
        if (managerBodyHwnd) ShowWindow(managerBodyHwnd, SW_HIDE);
    }
}

void LoadManagerSelectedNote() {
    auto* selNote = GetNoteById(managerSelectedId);
    if (selNote && managerTitleHwnd && managerBodyHwnd) {
        isLoadingEditor = true;
        SetWindowTextW(managerTitleHwnd, Wide(selNote->title).c_str());
        if (!selNote->rtf.empty()) {
            SetRichEditRtf(managerBodyHwnd, selNote->rtf);
        } else {
            SetWindowTextW(managerBodyHwnd, Wide(selNote->body).c_str());
        }
        isLoadingEditor = false;
        LayoutManagerControls(managerHwnd);
        if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE);
    } else {
        if (managerTitleHwnd) ShowWindow(managerTitleHwnd, SW_HIDE);
        if (managerBodyHwnd) ShowWindow(managerBodyHwnd, SW_HIDE);
        if (managerHwnd) InvalidateRect(managerHwnd, nullptr, TRUE);
    }
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

    // Theme resolution: 0 Auto (dark), 1 Light, 2 Dark
    bool isDark = (appearanceTheme != 1);
    COLORREF clrBg = isDark ? RGB(22, 23, 25) : RGB(248, 250, 252);
    COLORREF clrSidebar = isDark ? RGB(18, 19, 21) : RGB(241, 245, 249);
    COLORREF clrListCol = isDark ? RGB(24, 25, 28) : RGB(255, 255, 255);
    COLORREF clrPanel = isDark ? RGB(30, 32, 36) : RGB(255, 255, 255);
    COLORREF clrCardHov = isDark ? RGB(36, 38, 44) : RGB(243, 244, 246);
    COLORREF clrCardSel = isDark ? RGB(45, 48, 56) : RGB(226, 232, 240);
    COLORREF clrBorder = isDark ? RGB(44, 46, 53) : RGB(226, 232, 240);
    COLORREF clrText = isDark ? RGB(241, 245, 249) : RGB(15, 23, 42);
    COLORREF clrMuted = isDark ? RGB(148, 163, 184) : RGB(100, 116, 139);
    COLORREF clrHeaderBg = isDark ? RGB(28, 29, 34) : RGB(238, 242, 246);
    COLORREF clrAccent = RGB(99, 102, 241);

    // Main background
    Gdiplus::SolidBrush bgBrush(ToGdiColor(clrBg));
    g.FillRectangle(&bgBrush, 0, 0, client.right, client.bottom);

    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::SolidBrush textBrush(ToGdiColor(clrText));
    Gdiplus::SolidBrush mutedTextBrush(ToGdiColor(clrMuted));
    Gdiplus::Pen borderPen(ToGdiColor(clrBorder), 1.0f);
    Gdiplus::SolidBrush accentBrush(ToGdiColor(clrAccent));

    Gdiplus::StringFormat centerSf;
    centerSf.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    int pad = Scale(hwnd, 16);
    int bannerH = Scale(hwnd, 56);

    // -------------------------------------------------------------
    // TOP HEADER (Clean EdgeNotes brand & actions)
    // -------------------------------------------------------------
    Gdiplus::SolidBrush bannerBg(ToGdiColor(clrHeaderBg));
    g.FillRectangle(&bannerBg, 0, 0, client.right, bannerH);
    g.DrawLine(&borderPen, 0, bannerH, client.right, bannerH);

    // Brand Logo & Title on Left
    int logoSize = Scale(hwnd, 28);
    int logoX = pad;
    int logoY = (bannerH - logoSize) / 2;
    Gdiplus::RectF logoRect(static_cast<float>(logoX), static_cast<float>(logoY), static_cast<float>(logoSize), static_cast<float>(logoSize));
    Gdiplus::GraphicsPath logoPath;
    AddRoundedRectToPath(logoPath, logoRect, ScaleF(hwnd, 8.0f));
    g.FillPath(&accentBrush, &logoPath);

    Gdiplus::Font logoIconFont(L"Segoe UI", ScaleF(hwnd, 13.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    g.DrawString(L"\x229E", -1, &logoIconFont, logoRect, &centerSf, &whiteBrush);

    Gdiplus::Font brandTitleFont(L"Segoe UI", ScaleF(hwnd, 12.5f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    g.DrawString(L"EdgeNotes", -1, &brandTitleFont, Gdiplus::PointF(static_cast<float>(logoX + logoSize + Scale(hwnd, 10)), static_cast<float>(logoY - Scale(hwnd, 2))), &textBrush);

    Gdiplus::Font brandSubFont(L"Segoe UI", ScaleF(hwnd, 8.5f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    g.DrawString(L"Local & private \x00B7 Instant docked notes", -1, &brandSubFont, Gdiplus::PointF(static_cast<float>(logoX + logoSize + Scale(hwnd, 10)), static_cast<float>(logoY + Scale(hwnd, 15))), &mutedTextBrush);

    // Right Header Controls
    int btnH = Scale(hwnd, 30);
    int btnY = (bannerH - btnH) / 2;

    // Close button [✕]
    int closeSize = Scale(hwnd, 26);
    int closeX = client.right - pad - closeSize;
    Gdiplus::RectF closeRect(static_cast<float>(closeX), static_cast<float>((bannerH - closeSize) / 2), static_cast<float>(closeSize), static_cast<float>(closeSize));
    Gdiplus::Font closeFont(L"Segoe UI", ScaleF(hwnd, 12.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    g.DrawString(L"\x2715", -1, &closeFont, closeRect, &centerSf, &mutedTextBrush);

    // Theme toggle [☀ Light / ☾ Dark]
    int themeW = Scale(hwnd, 86);
    int themeX = closeX - themeW - Scale(hwnd, 10);
    Gdiplus::RectF themeRect(static_cast<float>(themeX), static_cast<float>(btnY), static_cast<float>(themeW), static_cast<float>(btnH));
    Gdiplus::GraphicsPath themePath;
    AddRoundedRectToPath(themePath, themeRect, ScaleF(hwnd, 6.0f));
    Gdiplus::SolidBrush themeBg(ToGdiColor(clrPanel));
    g.FillPath(&themeBg, &themePath);
    g.DrawPath(&borderPen, &themePath);
    Gdiplus::Font themeFont(L"Segoe UI", ScaleF(hwnd, 9.5f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    const wchar_t* themeLabel = (appearanceTheme == 1) ? L"\x2600 Light" : L"\x263E Dark";
    g.DrawString(themeLabel, -1, &themeFont, themeRect, &centerSf, &textBrush);

    // Export Backup button
    int bkBtnW = Scale(hwnd, 114);
    int bkBtnX = themeX - bkBtnW - Scale(hwnd, 10);
    Gdiplus::RectF bkRect(static_cast<float>(bkBtnX), static_cast<float>(btnY), static_cast<float>(bkBtnW), static_cast<float>(btnH));
    Gdiplus::GraphicsPath bkPath;
    AddRoundedRectToPath(bkPath, bkRect, ScaleF(hwnd, 6.0f));
    Gdiplus::SolidBrush bkBg(ToGdiColor(clrPanel));
    g.FillPath(&bkBg, &bkPath);
    g.DrawPath(&borderPen, &bkPath);
    g.DrawString(L"Export Backup", -1, &themeFont, bkRect, &centerSf, &textBrush);

    // [+ New note] button
    int newBtnW = Scale(hwnd, 108);
    int newBtnX = bkBtnX - newBtnW - Scale(hwnd, 10);
    Gdiplus::RectF newRect(static_cast<float>(newBtnX), static_cast<float>(btnY), static_cast<float>(newBtnW), static_cast<float>(btnH));
    Gdiplus::GraphicsPath newPath;
    AddRoundedRectToPath(newPath, newRect, ScaleF(hwnd, 6.0f));
    g.FillPath(&accentBrush, &newPath);
    Gdiplus::Font newBtnFont(L"Segoe UI", ScaleF(hwnd, 10.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    g.DrawString(L"+ New note", -1, &newBtnFont, newRect, &centerSf, &whiteBrush);

    // -------------------------------------------------------------
    // LEFT SIDEBAR
    // -------------------------------------------------------------
    int col1W = Scale(hwnd, 190);
    int bodyY = bannerH;
    int bodyH = client.bottom - bodyY;

    Gdiplus::SolidBrush sideBg(ToGdiColor(clrSidebar));
    g.FillRectangle(&sideBg, 0, bodyY, col1W, bodyH);
    g.DrawLine(&borderPen, col1W, bodyY, col1W, client.bottom);

    // NOTES Section header
    Gdiplus::Font sideGroupFont(L"Segoe UI", ScaleF(hwnd, 8.5f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    g.DrawString(L"NOTES", -1, &sideGroupFont, Gdiplus::PointF(static_cast<float>(pad), static_cast<float>(bodyY + Scale(hwnd, 16))), &mutedTextBrush);

    struct NavItemDef { int id; const wchar_t* icon; const wchar_t* label; };
    NavItemDef navs[] = {
        {CmdNavNotes, L"\x229E", L"All notes"},
        {CmdNavDeck, L"\x2691", L"On the deck"},
        {CmdNavArchive, L"\x25A4", L"Archive"}
    };

    int navY = bodyY + Scale(hwnd, 36);
    int navH = Scale(hwnd, 32);
    Gdiplus::Font navFont(L"Segoe UI", ScaleF(hwnd, 10.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

    for (const auto& item : navs) {
        bool isSel = false;
        if (managerNavSection == 0) {
            if (item.id == CmdNavNotes && managerFilter == CmdFilterAll) isSel = true;
            else if (item.id == CmdNavDeck && managerFilter == CmdFilterDeck) isSel = true;
            else if (item.id == CmdNavArchive && managerFilter == CmdFilterArchive) isSel = true;
        }

        Gdiplus::RectF nr(static_cast<float>(pad - 4), static_cast<float>(navY), static_cast<float>(col1W - pad * 2 + 8), static_cast<float>(navH));
        if (isSel) {
            Gdiplus::GraphicsPath np;
            AddRoundedRectToPath(np, nr, ScaleF(hwnd, 6.0f));
            g.FillPath(&accentBrush, &np);
        }

        Gdiplus::PointF iconPt(nr.X + Scale(hwnd, 10), nr.Y + Scale(hwnd, 7));
        g.DrawString(item.icon, -1, &navFont, iconPt, isSel ? &whiteBrush : &mutedTextBrush);

        Gdiplus::PointF textPt(nr.X + Scale(hwnd, 32), nr.Y + Scale(hwnd, 7));
        g.DrawString(item.label, -1, &navFont, textPt, isSel ? &whiteBrush : &textBrush);

        navY += navH + Scale(hwnd, 4);
    }

    // Bottom Sidebar Items: Settings, About, Storage Vault
    int botY = client.bottom - Scale(hwnd, 126);

    // Settings
    bool isSettingsSel = (managerNavSection == 1);
    Gdiplus::RectF setRect(static_cast<float>(pad - 4), static_cast<float>(botY), static_cast<float>(col1W - pad * 2 + 8), static_cast<float>(navH));
    if (isSettingsSel) {
        Gdiplus::GraphicsPath sp;
        AddRoundedRectToPath(sp, setRect, ScaleF(hwnd, 6.0f));
        g.FillPath(&accentBrush, &sp);
    }
    g.DrawString(L"\x2699", -1, &navFont, Gdiplus::PointF(setRect.X + Scale(hwnd, 10), setRect.Y + Scale(hwnd, 7)), isSettingsSel ? &whiteBrush : &mutedTextBrush);
    g.DrawString(L"Settings", -1, &navFont, Gdiplus::PointF(setRect.X + Scale(hwnd, 32), setRect.Y + Scale(hwnd, 7)), isSettingsSel ? &whiteBrush : &textBrush);

    // About
    botY += navH + Scale(hwnd, 4);
    bool isAboutSel = (managerNavSection == 2);
    Gdiplus::RectF abRect(static_cast<float>(pad - 4), static_cast<float>(botY), static_cast<float>(col1W - pad * 2 + 8), static_cast<float>(navH));
    if (isAboutSel) {
        Gdiplus::GraphicsPath ap;
        AddRoundedRectToPath(ap, abRect, ScaleF(hwnd, 6.0f));
        g.FillPath(&accentBrush, &ap);
    }
    g.DrawString(L"\x2139", -1, &navFont, Gdiplus::PointF(abRect.X + Scale(hwnd, 10), abRect.Y + Scale(hwnd, 7)), isAboutSel ? &whiteBrush : &mutedTextBrush);
    g.DrawString(L"About & Help", -1, &navFont, Gdiplus::PointF(abRect.X + Scale(hwnd, 32), abRect.Y + Scale(hwnd, 7)), isAboutSel ? &whiteBrush : &textBrush);

    // Storage Vault
    botY += navH + Scale(hwnd, 6);
    bool isVaultSel = (managerNavSection == 3);
    Gdiplus::RectF vaultRect(static_cast<float>(pad - 4), static_cast<float>(botY), static_cast<float>(col1W - pad * 2 + 8), static_cast<float>(navH));
    if (isVaultSel) {
        Gdiplus::GraphicsPath vp;
        AddRoundedRectToPath(vp, vaultRect, ScaleF(hwnd, 6.0f));
        g.FillPath(&accentBrush, &vp);
    }
    g.DrawString(L"\xD83D\xDD12", -1, &navFont, Gdiplus::PointF(vaultRect.X + Scale(hwnd, 10), vaultRect.Y + Scale(hwnd, 7)), isVaultSel ? &whiteBrush : &mutedTextBrush);
    g.DrawString(L"Storage Vault", -1, &navFont, Gdiplus::PointF(vaultRect.X + Scale(hwnd, 32), vaultRect.Y + Scale(hwnd, 7)), isVaultSel ? &whiteBrush : &textBrush);

    // -------------------------------------------------------------
    // RIGHT CONTENT: SETTINGS (1), ABOUT (2), VAULT (3), OR NOTES LIST (0)
    // -------------------------------------------------------------
    if (managerNavSection == 1) {
        // --- SETTINGS VIEW ---
        int setX = col1W + Scale(hwnd, 28);
        int curSetY = bodyY + Scale(hwnd, 22);
        int setContentW = client.right - col1W - Scale(hwnd, 56);

        Gdiplus::Font setHeaderFont(L"Segoe UI", ScaleF(hwnd, 20.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        g.DrawString(L"Settings", -1, &setHeaderFont, Gdiplus::PointF(static_cast<float>(setX), static_cast<float>(curSetY)), &textBrush);
        curSetY += Scale(hwnd, 28);

        Gdiplus::Font setSubFont(L"Segoe UI", ScaleF(hwnd, 10.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        g.DrawString(L"How EdgeNotes behaves on this PC.", -1, &setSubFont, Gdiplus::PointF(static_cast<float>(setX), static_cast<float>(curSetY)), &mutedTextBrush);
        curSetY += Scale(hwnd, 28);

        auto drawCard = [&](int y, int h) {
            Gdiplus::RectF cr(static_cast<float>(setX), static_cast<float>(y), static_cast<float>(setContentW), static_cast<float>(h));
            Gdiplus::GraphicsPath cp;
            AddRoundedRectToPath(cp, cr, ScaleF(hwnd, 8.0f));
            Gdiplus::SolidBrush cardBg(ToGdiColor(clrPanel));
            g.FillPath(&cardBg, &cp);
            g.DrawPath(&borderPen, &cp);
        };

        Gdiplus::Font secHeadFont(L"Segoe UI", ScaleF(hwnd, 11.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font itemTitleFont(L"Segoe UI", ScaleF(hwnd, 10.5f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font itemSubFont(L"Segoe UI", ScaleF(hwnd, 9.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font pillFont(L"Segoe UI", ScaleF(hwnd, 9.5f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

        // Group 1: Appearance
        g.DrawString(L"Appearance", -1, &secHeadFont, Gdiplus::PointF(static_cast<float>(setX), static_cast<float>(curSetY)), &textBrush);
        curSetY += Scale(hwnd, 20);

        int card1H = Scale(hwnd, 126);
        drawCard(curSetY, card1H);

        // Theme Row
        int r1Y = curSetY + Scale(hwnd, 14);
        g.DrawString(L"Theme", -1, &itemTitleFont, Gdiplus::PointF(static_cast<float>(setX + Scale(hwnd, 16)), static_cast<float>(r1Y)), &textBrush);
        g.DrawString(L"Follows Windows or select your preference.", -1, &itemSubFont, Gdiplus::PointF(static_cast<float>(setX + Scale(hwnd, 16)), static_cast<float>(r1Y + Scale(hwnd, 18))), &mutedTextBrush);

        const wchar_t* themePills[] = {L"Auto", L"Light", L"Dark"};
        int tPillW = Scale(hwnd, 52);
        int tPillH = Scale(hwnd, 24);
        int pillStartX = setX + setContentW - Scale(hwnd, 16) - tPillW * 3;
        for (int p = 0; p < 3; ++p) {
            Gdiplus::RectF pr(static_cast<float>(pillStartX + p * tPillW), static_cast<float>(r1Y + 4), static_cast<float>(tPillW), static_cast<float>(tPillH));
            if (appearanceTheme == p) {
                Gdiplus::GraphicsPath pp;
                AddRoundedRectToPath(pp, pr, ScaleF(hwnd, 4.0f));
                g.FillPath(&accentBrush, &pp);
                g.DrawString(themePills[p], -1, &pillFont, pr, &centerSf, &whiteBrush);
            } else {
                g.DrawString(themePills[p], -1, &pillFont, pr, &centerSf, &mutedTextBrush);
            }
        }

        // Font Row
        int r2Y = curSetY + Scale(hwnd, 68);
        g.DrawString(L"Font for new notes", -1, &itemTitleFont, Gdiplus::PointF(static_cast<float>(setX + Scale(hwnd, 16)), static_cast<float>(r2Y)), &textBrush);
        g.DrawString(L"Notes you have already written keep the hand they are in.", -1, &itemSubFont, Gdiplus::PointF(static_cast<float>(setX + Scale(hwnd, 16)), static_cast<float>(r2Y + Scale(hwnd, 18))), &mutedTextBrush);

        const wchar_t* fontPills[] = {L"Handwritten", L"Normal", L"Rounded", L"Typewriter"};
        int fPillW = Scale(hwnd, 76);
        int fPillStartX = setX + setContentW - Scale(hwnd, 16) - fPillW * 4;
        for (int p = 0; p < 4; ++p) {
            Gdiplus::RectF pr(static_cast<float>(fPillStartX + p * fPillW), static_cast<float>(r2Y + 4), static_cast<float>(fPillW), static_cast<float>(tPillH));
            if (noteFontChoice == p) {
                Gdiplus::GraphicsPath pp;
                AddRoundedRectToPath(pp, pr, ScaleF(hwnd, 4.0f));
                g.FillPath(&accentBrush, &pp);
                g.DrawString(fontPills[p], -1, &pillFont, pr, &centerSf, &whiteBrush);
            } else {
                g.DrawString(fontPills[p], -1, &pillFont, pr, &centerSf, &mutedTextBrush);
            }
        }

        curSetY += card1H + Scale(hwnd, 22);

        // Group 2: The deck
        g.DrawString(L"The deck", -1, &secHeadFont, Gdiplus::PointF(static_cast<float>(setX), static_cast<float>(curSetY)), &textBrush);
        curSetY += Scale(hwnd, 20);

        int card2H = Scale(hwnd, 126);
        drawCard(curSetY, card2H);

        // Screen Edge Row
        int d1Y = curSetY + Scale(hwnd, 14);
        g.DrawString(L"Screen edge", -1, &itemTitleFont, Gdiplus::PointF(static_cast<float>(setX + Scale(hwnd, 16)), static_cast<float>(d1Y)), &textBrush);
        g.DrawString(L"Which side of the display your notes live on.", -1, &itemSubFont, Gdiplus::PointF(static_cast<float>(setX + Scale(hwnd, 16)), static_cast<float>(d1Y + Scale(hwnd, 18))), &mutedTextBrush);

        const wchar_t* edgePills[] = {L"Left", L"Right"};
        int ePillW = Scale(hwnd, 52);
        int ePillStartX = setX + setContentW - Scale(hwnd, 16) - ePillW * 2;
        for (int p = 0; p < 2; ++p) {
            bool isRight = (config.side == DockSide::Right);
            bool isSel = (p == 0 ? !isRight : isRight);
            Gdiplus::RectF pr(static_cast<float>(ePillStartX + p * ePillW), static_cast<float>(d1Y + 4), static_cast<float>(ePillW), static_cast<float>(tPillH));
            if (isSel) {
                Gdiplus::GraphicsPath pp;
                AddRoundedRectToPath(pp, pr, ScaleF(hwnd, 4.0f));
                g.FillPath(&accentBrush, &pp);
                g.DrawString(edgePills[p], -1, &pillFont, pr, &centerSf, &whiteBrush);
            } else {
                g.DrawString(edgePills[p], -1, &pillFont, pr, &centerSf, &mutedTextBrush);
            }
        }

        // Fullscreen Hide Switch Row
        int d2Y = curSetY + Scale(hwnd, 68);
        g.DrawString(L"Hide over fullscreen apps", -1, &itemTitleFont, Gdiplus::PointF(static_cast<float>(setX + Scale(hwnd, 16)), static_cast<float>(d2Y)), &textBrush);
        g.DrawString(L"Keeps the deck out of games and presentations.", -1, &itemSubFont, Gdiplus::PointF(static_cast<float>(setX + Scale(hwnd, 16)), static_cast<float>(d2Y + Scale(hwnd, 18))), &mutedTextBrush);

        int swW = Scale(hwnd, 40);
        int swH = Scale(hwnd, 20);
        int swX = setX + setContentW - Scale(hwnd, 16) - swW;
        int swY = d2Y + Scale(hwnd, 6);
        Gdiplus::RectF swRect(static_cast<float>(swX), static_cast<float>(swY), static_cast<float>(swW), static_cast<float>(swH));
        Gdiplus::GraphicsPath swPath;
        AddRoundedRectToPath(swPath, swRect, static_cast<float>(swH) / 2.0f);
        Gdiplus::SolidBrush swBg(hideOverFullscreen ? ToGdiColor(clrAccent) : ToGdiColor(clrBorder));
        g.FillPath(&swBg, &swPath);
        float thumbX = hideOverFullscreen ? (swX + swW - swH + 2.0f) : (swX + 2.0f);
        g.FillEllipse(&whiteBrush, thumbX, static_cast<float>(swY + 2), static_cast<float>(swH - 4), static_cast<float>(swH - 4));

        curSetY += card2H + Scale(hwnd, 22);

        // Group 3: System & Storage
        g.DrawString(L"System & Storage", -1, &secHeadFont, Gdiplus::PointF(static_cast<float>(setX), static_cast<float>(curSetY)), &textBrush);
        curSetY += Scale(hwnd, 20);

        int card3H = Scale(hwnd, 116);
        drawCard(curSetY, card3H);

        int s1Y = curSetY + Scale(hwnd, 14);
        g.DrawString(L"Start with Windows", -1, &itemTitleFont, Gdiplus::PointF(static_cast<float>(setX + Scale(hwnd, 16)), static_cast<float>(s1Y)), &textBrush);
        g.DrawString(L"Adds a per-user entry to launch EdgeNotes automatically.", -1, &itemSubFont, Gdiplus::PointF(static_cast<float>(setX + Scale(hwnd, 16)), static_cast<float>(s1Y + Scale(hwnd, 18))), &mutedTextBrush);

        int sw3X = setX + setContentW - Scale(hwnd, 16) - swW;
        int sw3Y = s1Y + Scale(hwnd, 6);
        Gdiplus::RectF sw3Rect(static_cast<float>(sw3X), static_cast<float>(sw3Y), static_cast<float>(swW), static_cast<float>(swH));
        Gdiplus::GraphicsPath sw3Path;
        AddRoundedRectToPath(sw3Path, sw3Rect, static_cast<float>(swH) / 2.0f);
        Gdiplus::SolidBrush sw3Bg(startWithWindows ? ToGdiColor(clrAccent) : ToGdiColor(clrBorder));
        g.FillPath(&sw3Bg, &sw3Path);
        float thumb3X = startWithWindows ? (sw3X + swW - swH + 2.0f) : (sw3X + 2.0f);
        g.FillEllipse(&whiteBrush, thumb3X, static_cast<float>(sw3Y + 2), static_cast<float>(swH - 4), static_cast<float>(swH - 4));

        int s2Y = curSetY + Scale(hwnd, 64);
        int actBtnW = Scale(hwnd, 134);
        int actBtnH = Scale(hwnd, 28);
        int actX = setX + Scale(hwnd, 16);

        auto drawSubBtn = [&](int bx, const wchar_t* lbl) {
            Gdiplus::RectF ar(static_cast<float>(bx), static_cast<float>(s2Y), static_cast<float>(actBtnW), static_cast<float>(actBtnH));
            Gdiplus::GraphicsPath ap;
            AddRoundedRectToPath(ap, ar, ScaleF(hwnd, 4.0f));
            Gdiplus::SolidBrush bbg(ToGdiColor(clrCardHov));
            g.FillPath(&bbg, &ap);
            g.DrawPath(&borderPen, &ap);
            g.DrawString(lbl, -1, &pillFont, ar, &centerSf, &textBrush);
        };

        drawSubBtn(actX, L"Open Notes Folder");
        drawSubBtn(actX + actBtnW + Scale(hwnd, 12), L"Export Backup (JSON)");
        drawSubBtn(actX + (actBtnW + Scale(hwnd, 12)) * 2, L"Import Notes");
    } else if (managerNavSection == 2) {
        // --- ABOUT & HELP VIEW ---
        int setX = col1W + Scale(hwnd, 28);
        int curSetY = bodyY + Scale(hwnd, 22);
        int setContentW = client.right - col1W - Scale(hwnd, 56);

        Gdiplus::Font setHeaderFont(L"Segoe UI", ScaleF(hwnd, 20.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        g.DrawString(L"About & Help", -1, &setHeaderFont, Gdiplus::PointF(static_cast<float>(setX), static_cast<float>(curSetY)), &textBrush);
        curSetY += Scale(hwnd, 28);

        Gdiplus::Font setSubFont(L"Segoe UI", ScaleF(hwnd, 10.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        g.DrawString(L"EdgeNotes v1.4.1 \x2014 Fast, private Win32-native docked notes for Windows.", -1, &setSubFont, Gdiplus::PointF(static_cast<float>(setX), static_cast<float>(curSetY)), &mutedTextBrush);
        curSetY += Scale(hwnd, 28);

        auto drawCard = [&](int y, int h) {
            Gdiplus::RectF cr(static_cast<float>(setX), static_cast<float>(y), static_cast<float>(setContentW), static_cast<float>(h));
            Gdiplus::GraphicsPath cp;
            AddRoundedRectToPath(cp, cr, ScaleF(hwnd, 8.0f));
            Gdiplus::SolidBrush cardBg(ToGdiColor(clrPanel));
            g.FillPath(&cardBg, &cp);
            g.DrawPath(&borderPen, &cp);
        };

        Gdiplus::Font secHeadFont(L"Segoe UI", ScaleF(hwnd, 11.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font itemTitleFont(L"Segoe UI", ScaleF(hwnd, 10.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font itemSubFont(L"Segoe UI", ScaleF(hwnd, 9.5f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

        // Card 1: Keyboard Shortcuts Cheat Sheet
        g.DrawString(L"Keyboard Shortcuts", -1, &secHeadFont, Gdiplus::PointF(static_cast<float>(setX), static_cast<float>(curSetY)), &textBrush);
        curSetY += Scale(hwnd, 20);

        int card1H = Scale(hwnd, 160);
        drawCard(curSetY, card1H);

        struct ShortcutItem { const wchar_t* keys; const wchar_t* desc; };
        ShortcutItem scs[] = {
            {L"Ctrl + Alt + N", L"Create a new note instantly from anywhere"},
            {L"Ctrl + Alt + A", L"Open All Notes manager dashboard"},
            {L"Ctrl + Alt + L", L"Toggle screen edge dock visibility"},
            {L"Alt + Space", L"Toggle instant notes peek drawer"},
            {L"Ctrl + B / I / U / S", L"Bold, Italic, Underline, and Strike-through formatting"},
            {L"Escape", L"Close active editor or return dock to collapsed state"}
        };

        int scY = curSetY + Scale(hwnd, 12);
        for (const auto& sc : scs) {
            Gdiplus::RectF kr(static_cast<float>(setX + Scale(hwnd, 16)), static_cast<float>(scY), static_cast<float>(Scale(hwnd, 160)), static_cast<float>(Scale(hwnd, 20)));
            g.DrawString(sc.keys, -1, &itemTitleFont, kr, nullptr, &accentBrush);
            Gdiplus::RectF dr(static_cast<float>(setX + Scale(hwnd, 180)), static_cast<float>(scY), static_cast<float>(setContentW - Scale(hwnd, 196)), static_cast<float>(Scale(hwnd, 20)));
            g.DrawString(sc.desc, -1, &itemSubFont, dr, nullptr, &textBrush);
            scY += Scale(hwnd, 24);
        }

        curSetY += card1H + Scale(hwnd, 22);

        // Card 2: 100% Offline & Private
        g.DrawString(L"Privacy & Architecture", -1, &secHeadFont, Gdiplus::PointF(static_cast<float>(setX), static_cast<float>(curSetY)), &textBrush);
        curSetY += Scale(hwnd, 20);

        int card2H = Scale(hwnd, 92);
        drawCard(curSetY, card2H);

        g.DrawString(L"100% Offline & Zero Telemetry", -1, &itemTitleFont, Gdiplus::PointF(static_cast<float>(setX + Scale(hwnd, 16)), static_cast<float>(curSetY + Scale(hwnd, 14))), &textBrush);
        g.DrawString(L"All your notes are stored directly on your disk in standard JSON format. EdgeNotes never phones home or uploads your notes.", -1, &itemSubFont, Gdiplus::PointF(static_cast<float>(setX + Scale(hwnd, 16)), static_cast<float>(curSetY + Scale(hwnd, 36))), &mutedTextBrush);

        int abBtnW = Scale(hwnd, 150);
        int abBtnH = Scale(hwnd, 26);
        int abBtnY = curSetY + Scale(hwnd, 58);
        Gdiplus::RectF abBtn1(static_cast<float>(setX + Scale(hwnd, 16)), static_cast<float>(abBtnY), static_cast<float>(abBtnW), static_cast<float>(abBtnH));
        Gdiplus::GraphicsPath abp1;
        AddRoundedRectToPath(abp1, abBtn1, ScaleF(hwnd, 4.0f));
        Gdiplus::SolidBrush abBg(ToGdiColor(clrCardHov));
        g.FillPath(&abBg, &abp1);
        g.DrawPath(&borderPen, &abp1);
        Gdiplus::Font btnFont(L"Segoe UI", ScaleF(hwnd, 9.5f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        g.DrawString(L"Open Storage Folder", -1, &btnFont, abBtn1, &centerSf, &textBrush);

        Gdiplus::RectF abBtn2(static_cast<float>(setX + Scale(hwnd, 16) + abBtnW + Scale(hwnd, 12)), static_cast<float>(abBtnY), static_cast<float>(abBtnW), static_cast<float>(abBtnH));
        Gdiplus::GraphicsPath abp2;
        AddRoundedRectToPath(abp2, abBtn2, ScaleF(hwnd, 4.0f));
        g.FillPath(&abBg, &abp2);
        g.DrawPath(&borderPen, &abp2);
        g.DrawString(L"Export Backup JSON", -1, &btnFont, abBtn2, &centerSf, &textBrush);
    } else if (managerNavSection == 3) {
        // --- STORAGE VAULT VIEW ---
        int setX = col1W + Scale(hwnd, 28);
        int curSetY = bodyY + Scale(hwnd, 22);
        int setContentW = client.right - col1W - Scale(hwnd, 56);

        Gdiplus::Font setHeaderFont(L"Segoe UI", ScaleF(hwnd, 20.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        g.DrawString(L"Storage Vault", -1, &setHeaderFont, Gdiplus::PointF(static_cast<float>(setX), static_cast<float>(curSetY)), &textBrush);
        curSetY += Scale(hwnd, 28);

        Gdiplus::Font setSubFont(L"Segoe UI", ScaleF(hwnd, 10.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        g.DrawString(L"Overview of your local notes storage and export tools.", -1, &setSubFont, Gdiplus::PointF(static_cast<float>(setX), static_cast<float>(curSetY)), &mutedTextBrush);
        curSetY += Scale(hwnd, 28);

        auto drawCard = [&](int y, int h) {
            Gdiplus::RectF cr(static_cast<float>(setX), static_cast<float>(y), static_cast<float>(setContentW), static_cast<float>(h));
            Gdiplus::GraphicsPath cp;
            AddRoundedRectToPath(cp, cr, ScaleF(hwnd, 8.0f));
            Gdiplus::SolidBrush cardBg(ToGdiColor(clrPanel));
            g.FillPath(&cardBg, &cp);
            g.DrawPath(&borderPen, &cp);
        };

        Gdiplus::Font secHeadFont(L"Segoe UI", ScaleF(hwnd, 11.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font itemTitleFont(L"Segoe UI", ScaleF(hwnd, 10.5f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font itemSubFont(L"Segoe UI", ScaleF(hwnd, 9.5f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

        // Card 1: Statistics
        g.DrawString(L"Vault Statistics", -1, &secHeadFont, Gdiplus::PointF(static_cast<float>(setX), static_cast<float>(curSetY)), &textBrush);
        curSetY += Scale(hwnd, 20);

        int card1H = Scale(hwnd, 110);
        drawCard(curSetY, card1H);

        size_t activeCount = 0;
        size_t archivedCount = 0;
        for (const auto& n : store.notes) {
            if (n.archived) archivedCount++;
            else activeCount++;
        }

        std::wstring statLine1 = L"Total notes: " + std::to_wstring(store.notes.size()) + L"   \x00B7   Active on deck: " + std::to_wstring(activeCount) + L"   \x00B7   Archived: " + std::to_wstring(archivedCount);
        g.DrawString(statLine1.c_str(), -1, &itemTitleFont, Gdiplus::PointF(static_cast<float>(setX + Scale(hwnd, 16)), static_cast<float>(curSetY + Scale(hwnd, 14))), &textBrush);

        std::wstring statLine2 = CheckIsPortableMode() ? L"Storage Mode: Portable (next to EdgeNotes.exe)" : L"Storage Mode: Local AppData (%LocalAppData%\\EdgeNotes)";
        g.DrawString(statLine2.c_str(), -1, &itemSubFont, Gdiplus::PointF(static_cast<float>(setX + Scale(hwnd, 16)), static_cast<float>(curSetY + Scale(hwnd, 42))), &mutedTextBrush);

        std::wstring statLine3 = L"Security: 100% Offline, no cloud sync, human-readable UTF-8 JSON.";
        g.DrawString(statLine3.c_str(), -1, &itemSubFont, Gdiplus::PointF(static_cast<float>(setX + Scale(hwnd, 16)), static_cast<float>(curSetY + Scale(hwnd, 68))), &mutedTextBrush);

        curSetY += card1H + Scale(hwnd, 22);

        // Card 2: Operations
        g.DrawString(L"Vault Operations", -1, &secHeadFont, Gdiplus::PointF(static_cast<float>(setX), static_cast<float>(curSetY)), &textBrush);
        curSetY += Scale(hwnd, 20);

        int card2H = Scale(hwnd, 80);
        drawCard(curSetY, card2H);

        int vBtnW = Scale(hwnd, 150);
        int vBtnH = Scale(hwnd, 32);
        int vBtnY = curSetY + (card2H - vBtnH) / 2;
        int vx = setX + Scale(hwnd, 16);

        auto drawVaultBtn = [&](int xPos, const wchar_t* lbl, bool primary) {
            Gdiplus::RectF vr(static_cast<float>(xPos), static_cast<float>(vBtnY), static_cast<float>(vBtnW), static_cast<float>(vBtnH));
            Gdiplus::GraphicsPath vp;
            AddRoundedRectToPath(vp, vr, ScaleF(hwnd, 6.0f));
            if (primary) {
                g.FillPath(&accentBrush, &vp);
                g.DrawString(lbl, -1, &itemTitleFont, vr, &centerSf, &whiteBrush);
            } else {
                Gdiplus::SolidBrush bbg(ToGdiColor(clrCardHov));
                g.FillPath(&bbg, &vp);
                g.DrawPath(&borderPen, &vp);
                g.DrawString(lbl, -1, &itemTitleFont, vr, &centerSf, &textBrush);
            }
        };

        drawVaultBtn(vx, L"Export Backup (JSON)", true);
        drawVaultBtn(vx + vBtnW + Scale(hwnd, 12), L"Import Notes", false);
        drawVaultBtn(vx + (vBtnW + Scale(hwnd, 12)) * 2, L"Open Folder on Disk", false);
    } else {
        // --- 3-COLUMN NOTES LIST VIEW (managerNavSection == 0) ---
        int col2W = Scale(hwnd, 320);
        int col2X = col1W;

        Gdiplus::SolidBrush listBg(ToGdiColor(clrListCol));
        g.FillRectangle(&listBg, col2X, bodyY, col2W, bodyH);
        g.DrawLine(&borderPen, col2X + col2W, bodyY, col2X + col2W, client.bottom);

        int topRowY = bodyY + Scale(hwnd, 14);

        // Sort button ⇅
        int sortBtnX = col2X + col2W - Scale(hwnd, 72);
        int sortBtnSize = Scale(hwnd, 28);
        Gdiplus::Font sortFont(L"Segoe UI", ScaleF(hwnd, 12.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::RectF sortRect(static_cast<float>(sortBtnX), static_cast<float>(topRowY), static_cast<float>(sortBtnSize), static_cast<float>(sortBtnSize));
        g.DrawString(L"\x21C5", -1, &sortFont, sortRect, &centerSf, &mutedTextBrush);

        // Round [+] button
        int addBtnX = col2X + col2W - Scale(hwnd, 36);
        Gdiplus::RectF addRect(static_cast<float>(addBtnX), static_cast<float>(topRowY), static_cast<float>(sortBtnSize), static_cast<float>(sortBtnSize));
        g.FillEllipse(&accentBrush, addRect);
        Gdiplus::Font plusFont(L"Segoe UI", ScaleF(hwnd, 13.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        g.DrawString(L"+", -1, &plusFont, addRect, &centerSf, &whiteBrush);

        // Section Title
        int titleY = bodyY + Scale(hwnd, 54);
        const wchar_t* listTitle = L"All notes";
        if (managerFilter == CmdFilterDeck) listTitle = L"On the deck";
        else if (managerFilter == CmdFilterArchive) listTitle = L"Archive";

        Gdiplus::Font listTitleFont(L"Segoe UI", ScaleF(hwnd, 13.5f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        g.DrawString(listTitle, -1, &listTitleFont, Gdiplus::PointF(static_cast<float>(col2X + pad), static_cast<float>(titleY)), &textBrush);

        std::wstring countStr = std::to_wstring(managerRows.size()) + L" notes";
        Gdiplus::Font countFont(L"Segoe UI", ScaleF(hwnd, 9.5f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::RectF countRect(static_cast<float>(col2X + col2W - pad - Scale(hwnd, 80)), static_cast<float>(titleY + 2), static_cast<float>(Scale(hwnd, 80)), static_cast<float>(Scale(hwnd, 20)));
        Gdiplus::StringFormat countSf;
        countSf.SetAlignment(Gdiplus::StringAlignmentFar);
        g.DrawString(countStr.c_str(), -1, &countFont, countRect, &countSf, &mutedTextBrush);

        // List Cards
        int rowY = bodyY + Scale(hwnd, 86);
        int rowH = Scale(hwnd, 52);
        int cardPad = Scale(hwnd, 10);

        Gdiplus::Font cardTitleFont(L"Segoe UI", ScaleF(hwnd, 10.5f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font cardSubFont(L"Segoe UI", ScaleF(hwnd, 9.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font chipFont(L"Segoe UI", ScaleF(hwnd, 7.5f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

        for (std::size_t i = 0; i < managerRows.size(); ++i) {
            if (rowY + rowH > client.bottom) break;

            auto* n = GetNoteById(managerRows[i]);
            if (!n) continue;

            bool isSel = (managerSelectedId == n->id);
            bool isHov = (hoveredManagerRow == static_cast<int>(i));

            Gdiplus::RectF rr(static_cast<float>(col2X + cardPad), static_cast<float>(rowY), static_cast<float>(col2W - cardPad * 2), static_cast<float>(rowH));
            if (isSel) {
                Gdiplus::GraphicsPath rp;
                AddRoundedRectToPath(rp, rr, ScaleF(hwnd, 8.0f));
                Gdiplus::SolidBrush selBg(ToGdiColor(clrCardSel));
                g.FillPath(&selBg, &rp);
                Gdiplus::Pen selBorder(ToGdiColor(clrAccent), 1.0f);
                g.DrawPath(&selBorder, &rp);
            } else if (isHov) {
                Gdiplus::GraphicsPath rp;
                AddRoundedRectToPath(rp, rr, ScaleF(hwnd, 8.0f));
                Gdiplus::SolidBrush hovBg(ToGdiColor(clrCardHov));
                g.FillPath(&hovBg, &rp);
            }

            COLORREF cDot = PaperColors[n->color % 8];
            Gdiplus::SolidBrush dotB(ToGdiColor(cDot));
            float dotDiameter = static_cast<float>(Scale(hwnd, 8));
            g.FillEllipse(&dotB, rr.X + static_cast<float>(Scale(hwnd, 12)), rr.Y + static_cast<float>(Scale(hwnd, 14)), dotDiameter, dotDiameter);

            std::wstring itemTitle = Wide(n->title.empty() ? "Untitled note" : n->title);
            g.DrawString(itemTitle.c_str(), -1, &cardTitleFont, Gdiplus::PointF(rr.X + Scale(hwnd, 28), rr.Y + Scale(hwnd, 8)), &textBrush);

            std::string bodySnippet = n->body.empty() ? "Empty note" : n->body.substr(0, 36);
            std::wstring snippet = L"1d  " + Wide(bodySnippet);
            g.DrawString(snippet.c_str(), -1, &cardSubFont, Gdiplus::PointF(rr.X + Scale(hwnd, 28), rr.Y + Scale(hwnd, 28)), &mutedTextBrush);

            if (n->archived) {
                int chipW = Scale(hwnd, 58);
                int chipH = Scale(hwnd, 16);
                Gdiplus::RectF chipR(rr.X + rr.Width - chipW - Scale(hwnd, 10), rr.Y + Scale(hwnd, 10), static_cast<float>(chipW), static_cast<float>(chipH));
                Gdiplus::GraphicsPath chipPath;
                AddRoundedRectToPath(chipPath, chipR, ScaleF(hwnd, 4.0f));
                Gdiplus::SolidBrush chipBg(ToGdiColor(clrCardHov));
                g.FillPath(&chipBg, &chipPath);
                g.DrawString(L"ARCHIVED", -1, &chipFont, chipR, &centerSf, &mutedTextBrush);
            }

            rowY += rowH + Scale(hwnd, 4);
        }

        // Right Column: Preview Sheet or Empty State
        int col3X = col1W + col2W;
        int col3W = client.right - col3X;
        auto* selNote = GetNoteById(managerSelectedId);

        if (selNote) {
            int statY = bodyY + Scale(hwnd, 16);
            int prevPad = Scale(hwnd, 24);

            COLORREF cSelDot = PaperColors[selNote->color % 8];
            Gdiplus::SolidBrush sDotB(ToGdiColor(cSelDot));
            float statDotSize = static_cast<float>(Scale(hwnd, 8));
            g.FillEllipse(&sDotB, static_cast<float>(col3X + prevPad), static_cast<float>(statY + 2), statDotSize, statDotSize);

            Gdiplus::Font statFont(L"Segoe UI", ScaleF(hwnd, 9.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            std::wstring statTag = selNote->archived ? L"ARCHIVED" : L"ACTIVE \x00B7 ON THE DECK";
            g.DrawString(statTag.c_str(), -1, &statFont, Gdiplus::PointF(static_cast<float>(col3X + prevPad + statDotSize + Scale(hwnd, 8)), static_cast<float>(statY)), &mutedTextBrush);

            struct PrevAction { int id; const wchar_t* icon; };
            PrevAction prevActs[] = {
                {CmdPopoutNote, L"\x29C9"},
                {CmdMarkComplete, L"\x2713"},
                {CmdDeleteNote, L"\xD83D\xDDD1"}
            };

            int pActX = client.right - prevPad - Scale(hwnd, 24);
            int pActSize = Scale(hwnd, 22);
            for (int a = 2; a >= 0; --a) {
                Gdiplus::RectF ar(static_cast<float>(pActX), static_cast<float>(statY - 2), static_cast<float>(pActSize), static_cast<float>(pActSize));
                g.DrawString(prevActs[a].icon, -1, &sortFont, ar, &centerSf, &mutedTextBrush);
                pActX -= Scale(hwnd, 28);
            }

            int sheetY = statY + Scale(hwnd, 28);
            int sheetW = col3W - prevPad * 2;
            int sheetH = bodyH - Scale(hwnd, 72);

            Gdiplus::RectF sheetRect(static_cast<float>(col3X + prevPad), static_cast<float>(sheetY), static_cast<float>(sheetW), static_cast<float>(sheetH));
            Gdiplus::GraphicsPath sheetPath;
            AddRoundedRectToPath(sheetPath, sheetRect, ScaleF(hwnd, 16.0f));
            COLORREF sheetPaperColor = PaperColors[selNote->color % 8];
            Gdiplus::SolidBrush sheetBg(ToGdiColor(sheetPaperColor));
            g.FillPath(&sheetBg, &sheetPath);

            COLORREF sheetInkColor = PaperInks[selNote->color % 8];
            Gdiplus::SolidBrush sheetInk(ToGdiColor(sheetInkColor));
            Gdiplus::SolidBrush sheetMuted(ToGdiColor(sheetInkColor, 150));

            // Bottom toolbar inside sheet
            int sRow1Y = static_cast<int>(sheetRect.Y + sheetRect.Height) - Scale(hwnd, 64);
            int sRow1H = Scale(hwnd, 24);
            struct FormatBtn { int id; const wchar_t* label; };
            FormatBtn fbtns[] = {
                {CmdBoldText, L"B"},
                {CmdItalicText, L"I"},
                {CmdUnderlineText, L"U"},
                {CmdStrikeText, L"S"},
                {0, L"|"},
                {CmdChecklistText, L"\x2611"},
                {CmdBulletText, L":="},
                {CmdNumberedList, L"1="},
                {0, L"|"},
                {CmdInsertLink, L"\xD83D\xDD17"},
                {CmdNormalText, L"Aa"},
                {CmdMoreMenu, L"\x2026"}
            };

            int sfX = static_cast<int>(sheetRect.X) + Scale(hwnd, 20);
            Gdiplus::Font fmtFont(L"Segoe UI", ScaleF(hwnd, 11.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::Pen divPen(ToGdiColor(sheetInkColor, 40), 1.0f);

            for (const auto& fb : fbtns) {
                if (fb.id == 0) {
                    g.DrawLine(&divPen, sfX + Scale(hwnd, 5), sRow1Y + Scale(hwnd, 3), sfX + Scale(hwnd, 5), sRow1Y + sRow1H - Scale(hwnd, 3));
                    sfX += Scale(hwnd, 12);
                    continue;
                }
                int btnW = Scale(hwnd, 22);
                Gdiplus::RectF br(static_cast<float>(sfX), static_cast<float>(sRow1Y), static_cast<float>(btnW), static_cast<float>(sRow1H));
                g.DrawString(fb.label, -1, &fmtFont, br, &centerSf, &sheetInk);
                sfX += btnW + Scale(hwnd, 3);
            }

            int sRow2Y = static_cast<int>(sheetRect.Y + sheetRect.Height) - Scale(hwnd, 34);
            int sRow2H = Scale(hwnd, 24);
            Gdiplus::Font editedFont(L"Segoe UI", ScaleF(hwnd, 9.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            g.DrawString(L"Saved locally \x00B7 Click to edit", -1, &editedFont, Gdiplus::PointF(sheetRect.X + Scale(hwnd, 20), static_cast<float>(sRow2Y + 4)), &sheetMuted);

            int swSize = Scale(hwnd, 13);
            int swX = static_cast<int>(sheetRect.X + sheetRect.Width) - Scale(hwnd, 20) - (swSize + Scale(hwnd, 6)) * 8;
            int swY = sRow2Y + (sRow2H - swSize) / 2;

            for (int c = 0; c < 8; ++c) {
                Gdiplus::RectF sr(static_cast<float>(swX), static_cast<float>(swY), static_cast<float>(swSize), static_cast<float>(swSize));
                Gdiplus::SolidBrush sBrush(ToGdiColor(PaperColors[c]));
                g.FillEllipse(&sBrush, sr);
                if ((selNote->color % 8) == static_cast<unsigned>(c)) {
                    Gdiplus::Pen selPen(ToGdiColor(sheetInkColor), 2.0f);
                    Gdiplus::RectF ring(sr.X - 2.5f, sr.Y - 2.5f, sr.Width + 5.0f, sr.Height + 5.0f);
                    g.DrawEllipse(&selPen, ring);
                }
                swX += swSize + Scale(hwnd, 6);
            }
        } else {
            // Clean empty state
            int prevPad = Scale(hwnd, 32);
            int emptyX = col3X + prevPad;
            int emptyY = bodyY + Scale(hwnd, 60);
            int emptyW = col3W - prevPad * 2;
            int emptyH = bodyH - Scale(hwnd, 120);

            Gdiplus::RectF emptyRect(static_cast<float>(emptyX), static_cast<float>(emptyY), static_cast<float>(emptyW), static_cast<float>(emptyH));
            Gdiplus::GraphicsPath emptyPath;
            AddRoundedRectToPath(emptyPath, emptyRect, ScaleF(hwnd, 16.0f));
            Gdiplus::SolidBrush emptyBg(ToGdiColor(clrPanel));
            g.FillPath(&emptyBg, &emptyPath);
            g.DrawPath(&borderPen, &emptyPath);

            Gdiplus::Font iconFont(L"Segoe UI", ScaleF(hwnd, 36.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::RectF iconRect(static_cast<float>(emptyX), static_cast<float>(emptyY + Scale(hwnd, 50)), static_cast<float>(emptyW), static_cast<float>(Scale(hwnd, 50)));
            g.DrawString(L"\x270E", -1, &iconFont, iconRect, &centerSf, &mutedTextBrush);

            Gdiplus::Font emptyTitleFont(L"Segoe UI", ScaleF(hwnd, 16.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::RectF titleRect(static_cast<float>(emptyX), static_cast<float>(emptyY + Scale(hwnd, 110)), static_cast<float>(emptyW), static_cast<float>(Scale(hwnd, 28)));
            g.DrawString(L"No notes in this view", -1, &emptyTitleFont, titleRect, &centerSf, &textBrush);

            Gdiplus::Font emptySubFont(L"Segoe UI", ScaleF(hwnd, 10.5f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::RectF subRect(static_cast<float>(emptyX), static_cast<float>(emptyY + Scale(hwnd, 142)), static_cast<float>(emptyW), static_cast<float>(Scale(hwnd, 24)));
            g.DrawString(L"Click '+ New note' above to create your first note, or click the button below.", -1, &emptySubFont, subRect, &centerSf, &mutedTextBrush);

            int ctaW = Scale(hwnd, 150);
            int ctaH = Scale(hwnd, 34);
            int ctaX = emptyX + (emptyW - ctaW) / 2;
            int ctaY = emptyY + Scale(hwnd, 180);
            Gdiplus::RectF ctaRect(static_cast<float>(ctaX), static_cast<float>(ctaY), static_cast<float>(ctaW), static_cast<float>(ctaH));
            Gdiplus::GraphicsPath ctaPath;
            AddRoundedRectToPath(ctaPath, ctaRect, ScaleF(hwnd, 6.0f));
            g.FillPath(&accentBrush, &ctaPath);
            Gdiplus::Font ctaFont(L"Segoe UI", ScaleF(hwnd, 10.5f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            g.DrawString(L"+ Create a note", -1, &ctaFont, ctaRect, &centerSf, &whiteBrush);
        }
    }

    buffer.end(hdc);
    EndPaint(hwnd, &ps);
}

int HitTestManager(HWND hwnd, int x, int y) {
    RECT client;
    GetClientRect(hwnd, &client);
    int pad = Scale(hwnd, 16);
    int bannerH = Scale(hwnd, 56);

    // Header buttons (y >= 0 && y <= bannerH)
    if (y >= 0 && y <= bannerH) {
        int btnH = Scale(hwnd, 30);
        int btnY = (bannerH - btnH) / 2;

        int closeSize = Scale(hwnd, 26);
        int closeX = client.right - pad - closeSize;
        if (x >= closeX && x <= closeX + closeSize && y >= (bannerH - closeSize) / 2 && y <= (bannerH + closeSize) / 2) {
            return CmdCloseEditor;
        }

        int themeW = Scale(hwnd, 86);
        int themeX = closeX - themeW - Scale(hwnd, 10);
        if (x >= themeX && x <= themeX + themeW && y >= btnY && y <= btnY + btnH) {
            return CmdThemeToggle;
        }

        int bkBtnW = Scale(hwnd, 114);
        int bkBtnX = themeX - bkBtnW - Scale(hwnd, 10);
        if (x >= bkBtnX && x <= bkBtnX + bkBtnW && y >= btnY && y <= btnY + btnH) {
            return CmdExportBackupJson;
        }

        int newBtnW = Scale(hwnd, 108);
        int newBtnX = bkBtnX - newBtnW - Scale(hwnd, 10);
        if (x >= newBtnX && x <= newBtnX + newBtnW && y >= btnY && y <= btnY + btnH) {
            return CmdNewNote;
        }
    }

    int col1W = Scale(hwnd, 190);
    int bodyY = bannerH;

    // Left Sidebar (x >= 0 && x <= col1W)
    if (x >= 0 && x <= col1W) {
        int navY = bodyY + Scale(hwnd, 36);
        int navH = Scale(hwnd, 32);
        int navCmds[] = {CmdNavNotes, CmdNavDeck, CmdNavArchive};
        for (int i = 0; i < 3; ++i) {
            if (y >= navY && y <= navY + navH) return navCmds[i];
            navY += navH + Scale(hwnd, 4);
        }

        int botY = client.bottom - Scale(hwnd, 126);
        if (y >= botY && y <= botY + navH) return CmdNavSettings;
        botY += navH + Scale(hwnd, 4);
        if (y >= botY && y <= botY + navH) return CmdNavAbout;
        botY += navH + Scale(hwnd, 6);
        if (y >= botY && y <= client.bottom) return CmdNavSignIn;
    }

    // Settings (managerNavSection == 1)
    if (managerNavSection == 1) {
        int setX = col1W + Scale(hwnd, 28);
        int setContentW = client.right - col1W - Scale(hwnd, 56);
        int curSetY = bodyY + Scale(hwnd, 78);

        int card1H = Scale(hwnd, 126);
        int r1Y = curSetY + Scale(hwnd, 14);
        int tPillW = Scale(hwnd, 52);
        int tPillH = Scale(hwnd, 24);
        int pillStartX = setX + setContentW - Scale(hwnd, 16) - tPillW * 3;
        if (y >= r1Y + 4 && y <= r1Y + 4 + tPillH) {
            for (int p = 0; p < 3; ++p) {
                if (x >= pillStartX + p * tPillW && x <= pillStartX + (p + 1) * tPillW) {
                    return CmdThemeAuto + p;
                }
            }
        }

        int r2Y = curSetY + Scale(hwnd, 68);
        int fPillW = Scale(hwnd, 76);
        int fPillStartX = setX + setContentW - Scale(hwnd, 16) - fPillW * 4;
        if (y >= r2Y + 4 && y <= r2Y + 4 + tPillH) {
            for (int p = 0; p < 4; ++p) {
                if (x >= fPillStartX + p * fPillW && x <= fPillStartX + (p + 1) * fPillW) {
                    return CmdFontHandwritten + p;
                }
            }
        }

        curSetY += card1H + Scale(hwnd, 42);
        int card2H = Scale(hwnd, 126);
        int d1Y = curSetY + Scale(hwnd, 14);
        int ePillW = Scale(hwnd, 52);
        int ePillStartX = setX + setContentW - Scale(hwnd, 16) - ePillW * 2;
        if (y >= d1Y + 4 && y <= d1Y + 4 + tPillH) {
            if (x >= ePillStartX && x <= ePillStartX + ePillW) return CmdEdgeLeft;
            if (x >= ePillStartX + ePillW && x <= ePillStartX + ePillW * 2) return CmdEdgeRight;
        }

        int d2Y = curSetY + Scale(hwnd, 68);
        int swW = Scale(hwnd, 40);
        int swH = Scale(hwnd, 20);
        int swX = setX + setContentW - Scale(hwnd, 16) - swW;
        if (x >= swX && x <= swX + swW && y >= d2Y + 6 && y <= d2Y + 6 + swH) {
            return CmdToggleFullscreenHide;
        }

        curSetY += card2H + Scale(hwnd, 42);
        int s1Y = curSetY + Scale(hwnd, 14);
        int sw3X = setX + setContentW - Scale(hwnd, 16) - swW;
        if (x >= sw3X && x <= sw3X + swW && y >= s1Y + 6 && y <= s1Y + 6 + swH) {
            return CmdToggleStartWithWindows;
        }

        int s2Y = curSetY + Scale(hwnd, 64);
        int actBtnW = Scale(hwnd, 134);
        int actBtnH = Scale(hwnd, 28);
        int actX = setX + Scale(hwnd, 16);
        if (y >= s2Y && y <= s2Y + actBtnH) {
            if (x >= actX && x <= actX + actBtnW) return CmdOpenNotesFolder;
            if (x >= actX + actBtnW + Scale(hwnd, 12) && x <= actX + actBtnW * 2 + Scale(hwnd, 12)) return CmdExportBackupJson;
            if (x >= actX + (actBtnW + Scale(hwnd, 12)) * 2 && x <= actX + actBtnW * 3 + Scale(hwnd, 24)) return CmdImportNotes;
        }
    }

    // About (managerNavSection == 2)
    if (managerNavSection == 2) {
        int setX = col1W + Scale(hwnd, 28);
        int card1H = Scale(hwnd, 160);
        int card2Y = bodyY + Scale(hwnd, 78) + card1H + Scale(hwnd, 42);
        int abBtnW = Scale(hwnd, 150);
        int abBtnH = Scale(hwnd, 26);
        int abBtnY = card2Y + Scale(hwnd, 58);
        if (y >= abBtnY && y <= abBtnY + abBtnH) {
            int b1X = setX + Scale(hwnd, 16);
            if (x >= b1X && x <= b1X + abBtnW) return CmdOpenNotesFolder;
            int b2X = b1X + abBtnW + Scale(hwnd, 12);
            if (x >= b2X && x <= b2X + abBtnW) return CmdExportBackupJson;
        }
    }

    // Storage Vault (managerNavSection == 3)
    if (managerNavSection == 3) {
        int setX = col1W + Scale(hwnd, 28);
        int card1H = Scale(hwnd, 110);
        int card2Y = bodyY + Scale(hwnd, 78) + card1H + Scale(hwnd, 42);
        int card2H = Scale(hwnd, 80);
        int vBtnW = Scale(hwnd, 150);
        int vBtnH = Scale(hwnd, 32);
        int vBtnY = card2Y + (card2H - vBtnH) / 2;
        int vx = setX + Scale(hwnd, 16);
        if (y >= vBtnY && y <= vBtnY + vBtnH) {
            if (x >= vx && x <= vx + vBtnW) return CmdExportBackupJson;
            if (x >= vx + vBtnW + Scale(hwnd, 12) && x <= vx + vBtnW * 2 + Scale(hwnd, 12)) return CmdImportNotes;
            if (x >= vx + (vBtnW + Scale(hwnd, 12)) * 2 && x <= vx + vBtnW * 3 + Scale(hwnd, 24)) return CmdOpenNotesFolder;
        }
    }

    // Notes List View (managerNavSection == 0)
    if (managerNavSection == 0) {
        int col2W = Scale(hwnd, 320);
        int col2X = col1W;

        if (x >= col2X && x <= col2X + col2W) {
            int topRowY = bodyY + Scale(hwnd, 14);
            int sortBtnX = col2X + col2W - Scale(hwnd, 72);
            int sortBtnSize = Scale(hwnd, 28);
            if (x >= sortBtnX && x <= sortBtnX + sortBtnSize && y >= topRowY && y <= topRowY + sortBtnSize) {
                return CmdSortOrder;
            }

            int addBtnX = col2X + col2W - Scale(hwnd, 36);
            if (x >= addBtnX && x <= addBtnX + sortBtnSize && y >= topRowY && y <= topRowY + sortBtnSize) {
                return CmdNewNote;
            }

            int rowY = bodyY + Scale(hwnd, 86);
            int rowH = Scale(hwnd, 52);
            for (std::size_t i = 0; i < managerRows.size(); ++i) {
                if (y >= rowY && y <= rowY + rowH) {
                    return 1000 + static_cast<int>(i);
                }
                rowY += rowH + Scale(hwnd, 4);
            }
        }

        if (x > col2X + col2W) {
            auto* selNote = GetNoteById(managerSelectedId);
            if (selNote) {
                int statY = bodyY + Scale(hwnd, 16);
                int prevPad = Scale(hwnd, 24);
                int pActX = client.right - prevPad - Scale(hwnd, 24);
                int pActSize = Scale(hwnd, 22);
                int prevActs[] = {CmdDeleteNote, CmdMarkComplete, CmdPopoutNote};
                for (int a = 0; a < 3; ++a) {
                    if (x >= pActX && x <= pActX + pActSize && y >= statY - 4 && y <= statY + pActSize + 4) {
                        return prevActs[a];
                    }
                    pActX -= Scale(hwnd, 28);
                }

                int sheetW = client.right - (col1W + col2W) - prevPad * 2;
                int sheetH = (client.bottom - bodyY) - Scale(hwnd, 72);
                int sheetY = statY + Scale(hwnd, 28);

                // Format row (Row 1)
                int sRow1Y = sheetY + sheetH - Scale(hwnd, 64);
                int sRow1H = Scale(hwnd, 24);
                if (y >= sRow1Y && y <= sRow1Y + sRow1H) {
                    int sfX = (col1W + col2W + prevPad) + Scale(hwnd, 20);
                    int tools[] = {
                        CmdBoldText, CmdItalicText, CmdUnderlineText, CmdStrikeText,
                        0,
                        CmdChecklistText, CmdBulletText, CmdNumberedList,
                        0,
                        CmdInsertLink, CmdNormalText, CmdMoreMenu
                    };
                    for (int cmd : tools) {
                        if (cmd == 0) {
                            sfX += Scale(hwnd, 12);
                            continue;
                        }
                        int btnW = Scale(hwnd, 22);
                        if (x >= sfX && x <= sfX + btnW) return cmd;
                        sfX += btnW + Scale(hwnd, 3);
                    }
                }

                // Swatches row (Row 2)
                int sRow2Y = sheetY + sheetH - Scale(hwnd, 34);
                int swSize = Scale(hwnd, 13);
                int swX = (col1W + col2W + prevPad) + sheetW - Scale(hwnd, 20) - (swSize + Scale(hwnd, 6)) * 8;
                if (y >= sRow2Y - 4 && y <= sRow2Y + Scale(hwnd, 24)) {
                    for (int c = 0; c < 8; ++c) {
                        if (x >= swX - 4 && x <= swX + swSize + 4) {
                            return CmdColorBase + c;
                        }
                        swX += swSize + Scale(hwnd, 6);
                    }
                }
            } else {
                // Empty state CTA button [+ Create a note]
                int col3X = col1W + col2W;
                int col3W = client.right - col3X;
                int prevPad = Scale(hwnd, 32);
                int emptyX = col3X + prevPad;
                int emptyY = bodyY + Scale(hwnd, 60);
                int emptyW = col3W - prevPad * 2;
                int ctaW = Scale(hwnd, 150);
                int ctaH = Scale(hwnd, 34);
                int ctaX = emptyX + (emptyW - ctaW) / 2;
                int ctaY = emptyY + Scale(hwnd, 180);
                if (x >= ctaX && x <= ctaX + ctaW && y >= ctaY && y <= ctaY + ctaH) {
                    return CmdNewNote;
                }
            }
        }
    }

    return 0;
}

LRESULT CALLBACK ManagerWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            // Search Box
            searchBoxHwnd = CreateWindowExW(
                0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
                0, 0, 100, 28,
                hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(4)), appInstance, nullptr);
            SendMessageW(searchBoxHwnd, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search notes..."));
            SendMessageW(searchBoxHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);

            // Title Box in Sheet
            managerTitleHwnd = CreateWindowExW(
                0, L"EDIT", L"",
                WS_CHILD | ES_AUTOHSCROLL,
                0, 0, 100, 28,
                hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(5)), appInstance, nullptr);
            SendMessageW(managerTitleHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
            SendMessageW(managerTitleHwnd, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Untitled note"));
            SendMessageW(managerTitleHwnd, EM_LIMITTEXT, 200, 0);

            // Body RichEdit in Sheet
            managerBodyHwnd = CreateWindowExW(
                0, MSFTEDIT_CLASS, L"",
                WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                0, 0, 100, 100,
                hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(6)), appInstance, nullptr);
            SendMessageW(managerBodyHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(handFont), TRUE);
            SendMessageW(managerBodyHwnd, EM_SETEVENTMASK, 0, ENM_CHANGE);
            SendMessageW(managerBodyHwnd, EM_EXLIMITTEXT, 0, 16 * 1024 * 1024);

            LayoutManagerControls(hwnd);
            return 0;
        }

        case WM_SIZE: {
            LayoutManagerControls(hwnd);
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
            HWND ctl = reinterpret_cast<HWND>(lp);
            bool isDark = (appearanceTheme != 1);
            COLORREF panelBg = isDark ? RGB(30, 32, 36) : RGB(255, 255, 255);
            COLORREF textClr = isDark ? RGB(241, 245, 249) : RGB(15, 23, 42);

            auto* sel = GetNoteById(managerSelectedId);
            COLORREF paperColor = sel ? PaperColors[sel->color % 8] : panelBg;
            COLORREF inkColor = sel ? PaperInks[sel->color % 8] : textClr;

            HDC dc = reinterpret_cast<HDC>(wp);
            if (ctl == managerTitleHwnd || ctl == managerBodyHwnd) {
                SetBkMode(dc, OPAQUE);
                SetBkColor(dc, paperColor);
                SetTextColor(dc, inkColor);
                static HBRUSH s_mgrPaperBrush = nullptr;
                if (s_mgrPaperBrush) DeleteObject(s_mgrPaperBrush);
                s_mgrPaperBrush = CreateSolidBrush(paperColor);
                return reinterpret_cast<LRESULT>(s_mgrPaperBrush);
            } else {
                SetBkMode(dc, OPAQUE);
                SetBkColor(dc, panelBg);
                SetTextColor(dc, textClr);
                static HBRUSH s_mgrBrush = nullptr;
                if (s_mgrBrush) DeleteObject(s_mgrBrush);
                s_mgrBrush = CreateSolidBrush(panelBg);
                return reinterpret_cast<LRESULT>(s_mgrBrush);
            }
        }

        case WM_COMMAND: {
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            if (id == 4 && code == EN_CHANGE) {
                RefreshManagerList();
                LoadManagerSelectedNote();
            } else if (id == 5 && code == EN_CHANGE && !isLoadingEditor) {
                auto* sel = GetNoteById(managerSelectedId);
                if (sel) {
                    sel->title = Narrow(GetWindowTextString(managerTitleHwnd));
                    PersistStore();
                    UpdateDockGeometry();
                    if (dockHwnd) InvalidateRect(dockHwnd, nullptr, TRUE);
                }
            } else if (id == 6 && code == EN_CHANGE && !isLoadingEditor) {
                auto* sel = GetNoteById(managerSelectedId);
                if (sel) {
                    sel->body = Narrow(GetWindowTextString(managerBodyHwnd));
                    sel->rtf = GetRichEditRtf(managerBodyHwnd);
                    PersistStore();
                }
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

            if (hit == CmdCloseEditor) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            if (hit == CmdNewNote) {
                ShowWindow(hwnd, SW_HIDE);
                ExecuteCommand(CmdNewNote);
                return 0;
            }
            if (hit == CmdNavNotes) {
                managerNavSection = 0;
                managerFilter = CmdFilterAll;
                RefreshManagerList();
                LoadManagerSelectedNote();
                return 0;
            }
            if (hit == CmdNavDeck) {
                managerNavSection = 0;
                managerFilter = CmdFilterDeck;
                RefreshManagerList();
                LoadManagerSelectedNote();
                return 0;
            }
            if (hit == CmdNavArchive) {
                managerNavSection = 0;
                managerFilter = CmdFilterArchive;
                RefreshManagerList();
                LoadManagerSelectedNote();
                return 0;
            }
            if (hit == CmdNavSettings) {
                managerNavSection = 1;
                LayoutManagerControls(hwnd);
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            if (hit == CmdNavAbout) {
                managerNavSection = 2;
                LayoutManagerControls(hwnd);
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            if (hit == CmdNavSignIn) {
                managerNavSection = 3;
                LayoutManagerControls(hwnd);
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            if (hit == CmdPopoutNote) {
                if (managerSelectedId != 0) {
                    activeId = managerSelectedId;
                    ShowWindow(hwnd, SW_HIDE);
                    SetAppState(AppState::Editing);
                }
                return 0;
            }
            if (hit == CmdMarkComplete) {
                ExecuteCommand(CmdMarkComplete);
                LoadManagerSelectedNote();
                return 0;
            }
            if (hit == CmdDeleteNote) {
                ExecuteCommand(CmdDeleteNote);
                LoadManagerSelectedNote();
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
                if (choice) {
                    ExecuteCommand(choice);
                    LoadManagerSelectedNote();
                }
                return 0;
            }
            if (hit >= 1000 && hit < 2000) {
                std::size_t idx = static_cast<std::size_t>(hit - 1000);
                if (idx < managerRows.size()) {
                    managerSelectedId = managerRows[idx];
                    LoadManagerSelectedNote();
                }
                return 0;
            }
            if (hit != 0) {
                ExecuteCommand(hit);
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
    std::wstring exeDir = GetExeDir();
    std::wstring fontPath = exeDir + L"\\excalifont-regular.ttf";
    if (GetFileAttributesW(fontPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        AddFontResourceExW(fontPath.c_str(), FR_PRIVATE, 0);
    }

    uiFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    uiBoldFont = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    titleFont = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    handFont = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Excalifont");
    handPreviewFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Excalifont");
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

    // Read notes from storage (.enotes binary or notes.json)
    std::wstring jsonPath = baseDir + L"\\notes.json";
    bool loadedNotes = false;
    HANDLE hNotesFile = CreateFileW(storagePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hNotesFile != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        if (GetFileSizeEx(hNotesFile, &size) && size.QuadPart > 0 && size.QuadPart < 128 * 1024 * 1024) {
            std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
            DWORD readBytes = 0;
            if (ReadFile(hNotesFile, &bytes[0], static_cast<DWORD>(bytes.size()), &readBytes, nullptr) && (readBytes == bytes.size())) {
                try {
                    store.notes = edgenotes::decode(bytes);
                    loadedNotes = !store.notes.empty();
                } catch (...) {
                    // Protected corrupt file
                }
            }
        }
        CloseHandle(hNotesFile);
    }
    if (!loadedNotes) {
        HANDLE hJson = CreateFileW(jsonPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hJson != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER size{};
            if (GetFileSizeEx(hJson, &size) && size.QuadPart > 0 && size.QuadPart < 128 * 1024 * 1024) {
                std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
                DWORD readBytes = 0;
                if (ReadFile(hJson, &bytes[0], static_cast<DWORD>(bytes.size()), &readBytes, nullptr) && (readBytes == bytes.size())) {
                    try {
                        store.notes = edgenotes::decodeJson(bytes);
                        loadedNotes = !store.notes.empty();
                    } catch (...) {
                    }
                }
            }
            CloseHandle(hJson);
        }
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

    WNDCLASSW wcUndo{0};
    wcUndo.lpfnWndProc = UndoWndProc;
    wcUndo.hInstance = inst;
    wcUndo.lpszClassName = L"EdgeUndoToast";
    wcUndo.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&wcUndo);

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

    undoHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"EdgeUndoToast", L"EdgeNotes Undo",
        WS_POPUP, 0, 0, 310, 44, nullptr, nullptr, inst, nullptr);

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
    if (IsWindow(undoHwnd)) DestroyWindow(undoHwnd);
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