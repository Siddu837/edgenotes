#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <cwctype>
#include <cstring>
#include <sstream>
#include "note-store.hpp"

// Native Windows controls only: no WebView, networking, polling or timers.
namespace {
using edgenotes::Note;
enum class DeckState { Collapsed, Peek, Editing };
enum { NewNote = 101, AllNotes, ArchiveView, DeleteNote, UndoDelete, CycleColor, ExportNote, DockLeft, DockRight, ExitApp, BoldText, ItalicText, BulletText, HeadingText, RestoreNote, FilterAll, FilterDeck, FilterArchive };
constexpr UINT TrayMessage = WM_APP + 1;
const COLORREF Paper[] = {RGB(185,228,208), RGB(191,220,245), RGB(220,203,241), RGB(248,233,165), RGB(245,203,213), RGB(250,211,170), RGB(184,225,226), RGB(224,230,237)};
const COLORREF Ink = RGB(42,57,55), Night = RGB(27,28,30), NightText = RGB(226,230,230);
HINSTANCE instance;
HWND dock, editorWindow, managerWindow, titleBox, richEdit, listBox, searchBox, statusBox;
HFONT uiFont, handFont;
HBRUSH nightBrush;
HICON trayIcon;
HMODULE richLibrary;
edgenotes::Store store;
DeckState state = DeckState::Collapsed;
std::uint64_t activeId = 0, nextId = 1;
std::vector<std::uint64_t> rows;
bool loading = false, leftSide = false, saveFailed = false, modalOpen = false;
int filter = FilterAll;
std::wstring storagePath;
Note* active() { for (auto& n : store.notes) if (n.id == activeId) return &n; return nullptr; }
std::wstring wide(const std::string& s) {
  if (s.empty()) return {};
  int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (!count) return L"";
  std::wstring out(count, L'\0'); MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), count); return out;
}
std::string utf8(const std::wstring& s) {
  if (s.empty()) return {};
  int count = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
  std::string out(count, '\0'); WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), count, nullptr, nullptr); return out;
}
std::wstring text(HWND hwnd) {
  int len = GetWindowTextLengthW(hwnd); std::wstring out(len + 1, L'\0');
  GetWindowTextW(hwnd, out.data(), len + 1); out.resize(len); return out;
}
void status() {
  auto* n = active(); if (!n || !statusBox) return;
  bool undo = store.deleted && GetTickCount64() < store.deleted->expires;
  std::wstring caption = saveFailed ? L"Save failed - export your note" : L"Saved locally";
  caption += L"  |  " + std::to_wstring(wide(n->body).size()) + L" characters";
  if (undo) caption += L"  |  Undo deletion: Ctrl+Z";
  SetWindowTextW(statusBox, caption.c_str());
}
bool atomicWrite(const std::wstring& path, const std::string& data) {
  auto temp = path + L".tmp";
  HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0; bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
  if (ok) ok = FlushFileBuffers(h) != 0;
  CloseHandle(h);
  if (ok) ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  if (!ok) DeleteFileW(temp.c_str());
  return ok;
}
void persist() {
  try { saveFailed = !atomicWrite(storagePath, edgenotes::encode(store.notes)); }
  catch (...) { saveFailed = true; }
  status(); InvalidateRect(dock, nullptr, TRUE);
}
struct Stream { std::string bytes; std::size_t offset = 0; };
DWORD CALLBACK streamOut(DWORD_PTR cookie, LPBYTE buffer, LONG count, LONG* done) {
  auto* s = reinterpret_cast<Stream*>(cookie); s->bytes.append(reinterpret_cast<char*>(buffer), count); *done = count; return 0;
}
DWORD CALLBACK streamIn(DWORD_PTR cookie, LPBYTE buffer, LONG count, LONG* done) {
  auto* s = reinterpret_cast<Stream*>(cookie); auto size = std::min(static_cast<std::size_t>(count), s->bytes.size() - s->offset);
  std::memcpy(buffer, s->bytes.data() + s->offset, size); s->offset += size; *done = static_cast<LONG>(size); return 0;
}
void saveEditor() {
  if (loading) return;
  auto* n = active(); if (!n) return;
  n->title = utf8(text(titleBox)); n->body = utf8(text(richEdit));
  Stream stream; EDITSTREAM es{}; es.dwCookie = reinterpret_cast<DWORD_PTR>(&stream); es.pfnCallback = streamOut;
  SendMessageW(richEdit, EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&es));
  if (es.dwError) { saveFailed = true; status(); return; }
  n->rtf = std::move(stream.bytes); persist();
}
void loadEditor() {
  auto* n = active(); if (!n) return; loading = true;
  SetWindowTextW(titleBox, wide(n->title).c_str());
  SendMessageW(richEdit, EM_SETBKGNDCOLOR, 0, Paper[n->color % 8]);
  if (n->rtf.empty()) SetWindowTextW(richEdit, wide(n->body).c_str());
  else { Stream stream{n->rtf}; EDITSTREAM es{}; es.dwCookie = reinterpret_cast<DWORD_PTR>(&stream); es.pfnCallback = streamIn; SendMessageW(richEdit, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es)); }
  CHARFORMAT2W fmt{}; fmt.cbSize = sizeof(fmt); fmt.dwMask = CFM_COLOR; fmt.crTextColor = Ink;
  SendMessageW(richEdit, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&fmt));
  SendMessageW(richEdit, EM_EMPTYUNDOBUFFER, 0, 0); loading = false; status(); InvalidateRect(editorWindow, nullptr, TRUE);
}
RECT workArea() { MONITORINFO info{}; info.cbSize = sizeof(info); GetMonitorInfoW(MonitorFromWindow(dock, MONITOR_DEFAULTTONEAREST), &info); return info.rcWork; }
void setState(DeckState next) {
  state = next; RECT area = workArea();
  int height = std::min(540, static_cast<int>(area.bottom - area.top - 50));
  int width = next == DeckState::Peek ? 280 : 34;
  SetWindowPos(dock, HWND_TOPMOST, leftSide ? area.left : area.right - width, area.top + 80, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(dock, nullptr, TRUE);
  if (next == DeckState::Editing) {
    loadEditor(); RECT r; GetWindowRect(editorWindow, &r);
    int w = std::min(510, static_cast<int>(area.right - area.left - 80));
    SetWindowPos(editorWindow, HWND_TOPMOST, leftSide ? area.left + 55 : area.right - w - 55, area.top + 80, w, height, SWP_NOACTIVATE);
    BOOL animate = TRUE; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0);
    if (!IsWindowVisible(editorWindow) && animate) AnimateWindow(editorWindow, 140, AW_BLEND);
    ShowWindow(editorWindow, SW_SHOW); SetForegroundWindow(editorWindow); SetFocus(richEdit);
  } else ShowWindow(editorWindow, SW_HIDE);
}
void refreshList() {
  if (!listBox) return;
  auto query = text(searchBox); std::transform(query.begin(), query.end(), query.begin(), [](wchar_t c) { return std::towlower(c); });
  rows.clear(); SendMessageW(listBox, LB_RESETCONTENT, 0, 0);
  for (const auto& n : store.notes) {
    if ((filter == FilterDeck && n.archived) || (filter == FilterArchive && !n.archived)) continue;
    auto haystack = wide(n.title + " " + n.body); std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](wchar_t c) { return std::towlower(c); });
    if (haystack.find(query) == std::wstring::npos) continue;
    auto label = wide(n.title.empty() ? "Untitled note" : n.title); SendMessageW(listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())); rows.push_back(n.id);
  }
}
void showManager(int mode = FilterAll) { filter = mode; refreshList(); ShowWindow(managerWindow, SW_SHOW); SetForegroundWindow(managerWindow); SetFocus(searchBox); }
void format(DWORD mask, DWORD effect) {
  CHARFORMAT2W fmt{}; fmt.cbSize = sizeof(fmt); fmt.dwMask = mask;
  SendMessageW(richEdit, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt));
  fmt.dwEffects ^= effect; fmt.dwMask = mask;
  SendMessageW(richEdit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt)); SetFocus(richEdit); saveEditor();
}
void command(int id) {
  if (store.deleted && GetTickCount64() >= store.deleted->expires) store.deleted.reset();
  auto* n = active();
  switch (id) {
    case NewNote: { Note note; note.id = nextId++; note.title = "Untitled note"; note.color = static_cast<unsigned>(store.notes.size() % 8); activeId = note.id; store.notes.push_back(note); persist(); setState(DeckState::Editing); SetFocus(titleBox); SendMessageW(titleBox, EM_SETSEL, 0, -1); break; }
    case AllNotes: showManager(); break;
    case ArchiveView: showManager(FilterArchive); break;
    case DeleteNote: if (n) { store.erase(n->id, GetTickCount64()); activeId = store.notes.empty() ? 0 : store.notes.front().id; persist(); refreshList(); if (active()) loadEditor(); else setState(DeckState::Collapsed); } break;
    case UndoDelete: if (store.deleted) { auto idToRestore = store.deleted->note.id; if (store.undo(GetTickCount64())) { activeId = idToRestore; persist(); refreshList(); setState(DeckState::Editing); } } break;
    case CycleColor: if (n) { n->color = (n->color + 1) % 8; persist(); loadEditor(); } break;
    case RestoreNote: if (n) { n->archived = !n->archived; persist(); refreshList(); } break;
    case DockLeft: leftSide = true; setState(state); break;
    case DockRight: leftSide = false; setState(state); break;
    case ExitApp: DestroyWindow(dock); break;
    case BoldText: format(CFM_BOLD, CFE_BOLD); break;
    case ItalicText: format(CFM_ITALIC, CFE_ITALIC); break;
    case BulletText: { PARAFORMAT2 fmt{}; fmt.cbSize = sizeof(fmt); fmt.dwMask = PFM_NUMBERING; fmt.wNumbering = PFN_BULLET; SendMessageW(richEdit, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&fmt)); saveEditor(); SetFocus(richEdit); break; }
    case HeadingText: { CHARFORMAT2W fmt{}; fmt.cbSize = sizeof(fmt); fmt.dwMask = CFM_SIZE | CFM_BOLD; fmt.yHeight = 480; fmt.dwEffects = CFE_BOLD; SendMessageW(richEdit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt)); saveEditor(); SetFocus(richEdit); break; }
    case ExportNote: if (n) { wchar_t path[MAX_PATH] = L"EdgeNotes-note.txt"; OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = editorWindow; ofn.lpstrFilter = L"Plain text (*.txt)\0*.txt\0Rich text (*.rtf)\0*.rtf\0\0"; ofn.lpstrFile = path; ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_OVERWRITEPROMPT; ofn.lpstrDefExt = L"txt"; modalOpen = true; if (GetSaveFileNameW(&ofn)) { bool rtf = ofn.nFilterIndex == 2; if (!atomicWrite(path, rtf ? n->rtf : n->title + "\r\n\r\n" + n->body)) MessageBoxW(editorWindow,L"Could not export this note.",L"EdgeNotes",MB_ICONERROR); } modalOpen = false; } break;
    case FilterAll: case FilterDeck: case FilterArchive: filter = id; refreshList(); break;
  }
}
LRESULT CALLBACK editSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
  if (msg == WM_KEYDOWN) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (wp == VK_ESCAPE) { setState(DeckState::Collapsed); return 0; }
    if (ctrl && wp == VK_BACK) { command(DeleteNote); return 0; }
    if (ctrl && wp == VK_OEM_PERIOD) { command(CycleColor); return 0; }
    if (ctrl && wp == 'Z' && store.deleted) { command(UndoDelete); return 0; }
    if (ctrl && wp == 'B') { command(BoldText); return 0; }
    if (ctrl && wp == 'I') { command(ItalicText); return 0; }
  }
  if (msg == WM_CHAR && wp == ' ' && hwnd == richEdit) {
    CHARRANGE selection{}; SendMessageW(hwnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    LONG line = static_cast<LONG>(SendMessageW(hwnd, EM_LINEFROMCHAR, selection.cpMin, 0));
    LONG start = static_cast<LONG>(SendMessageW(hwnd, EM_LINEINDEX, line, 0));
    LONG len = selection.cpMin - start;
    if (selection.cpMin == selection.cpMax && len > 0 && len <= 3) {
      wchar_t buf[8]{}; TEXTRANGEW range{{start, selection.cpMin}, buf}; SendMessageW(hwnd, EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&range));
      std::wstring trigger(buf);
      if (trigger == L"#" || trigger == L"##" || trigger == L"###" || trigger == L"-" || trigger == L"*") {
        loading = true; CHARRANGE remove{start, selection.cpMin}; SendMessageW(hwnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&remove)); SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"")); loading = false;
        if (trigger == L"-" || trigger == L"*") command(BulletText);
        else { CHARFORMAT2W fmt{}; fmt.cbSize = sizeof(fmt); fmt.dwMask = CFM_SIZE | CFM_BOLD; fmt.dwEffects = CFE_BOLD; fmt.yHeight = trigger.size() == 1 ? 520 : trigger.size() == 2 ? 440 : 380; SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt)); saveEditor(); }
        return 0;
      }
    }
  }
  return DefSubclassProc(hwnd, msg, wp, lp);
}
void menu(HWND hwnd) {
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_STRING, NewNote, L"New note\tCtrl+Alt+N"); AppendMenuW(m, MF_STRING, AllNotes, L"All notes\tCtrl+Alt+A"); AppendMenuW(m, MF_STRING, ArchiveView, L"Archive\tCtrl+Alt+L");
  AppendMenuW(m, MF_STRING, UndoDelete, L"Undo deletion"); AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, DockLeft, L"Dock left"); AppendMenuW(m, MF_STRING, DockRight, L"Dock right"); AppendMenuW(m, MF_STRING, ExitApp, L"Quit EdgeNotes");
  POINT p; GetCursorPos(&p); SetForegroundWindow(hwnd); int choice = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr); DestroyMenu(m); if (choice) command(choice); PostMessageW(hwnd, WM_NULL, 0, 0);
}
HWND control(HWND parent, const wchar_t* cls, const wchar_t* label, DWORD style, int id, int x, int y, int w, int h) {
  HWND c = CreateWindowExW(0, cls, label, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
  SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE); return c;
}
void drawDock(HWND hwnd) {
  PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps); RECT client; GetClientRect(hwnd, &client);
  FillRect(dc, &client, nightBrush); SetBkMode(dc, TRANSPARENT); SelectObject(dc, uiFont);
  int y = 6, shown = 0;
  for (const auto& n : store.notes) {
    if (n.archived || shown >= 5) continue;
    RECT r{3, y, client.right - 2, y + 76}; auto brush = CreateSolidBrush(Paper[n.color % 8]); FillRect(dc, &r, brush); DeleteObject(brush); SetTextColor(dc, Ink);
    auto label = wide(n.title.empty() ? "Untitled note" : n.title); if (state == DeckState::Collapsed || state == DeckState::Editing) label = label.substr(0, 1);
    r.left += 6; r.right -= 5; DrawTextW(dc, label.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    y += 82; ++shown;
  }
  RECT add{0, y, client.right, y + 35}; SetTextColor(dc, NightText); DrawTextW(dc, L"+", -1, &add, DT_CENTER | DT_VCENTER | DT_SINGLELINE); EndPaint(hwnd, &ps);
}
LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_CREATE) {
    auto cls = reinterpret_cast<CREATESTRUCTW*>(lp)->lpszClass;
    if (std::wcscmp(cls, L"EdgeEditor") == 0) {
      titleBox = control(hwnd,L"EDIT",L"",ES_AUTOHSCROLL,1,20,12,340,30);
      const wchar_t* labels[] = {L"B",L"I",L"H",L"List",L"Color",L"Archive",L"Delete",L"Export"};
      int ids[] = {BoldText,ItalicText,HeadingText,BulletText,CycleColor,RestoreNote,DeleteNote,ExportNote};
      for (int i=0;i<8;++i) control(hwnd,L"BUTTON",labels[i],BS_PUSHBUTTON,ids[i],15+i*57,50,54,26);
      richEdit = control(hwnd,MSFTEDIT_CLASS,L"",ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN,2,20,90,460,380);
      SendMessageW(richEdit,WM_SETFONT,reinterpret_cast<WPARAM>(handFont),TRUE); SendMessageW(richEdit,EM_SETEVENTMASK,0,ENM_CHANGE); SendMessageW(richEdit,EM_EXLIMITTEXT,0,16*1024*1024);
      statusBox = control(hwnd,L"STATIC",L"Saved locally",0,3,20,480,460,25);
      SetWindowSubclass(richEdit,editSubclass,1,0); SetWindowSubclass(titleBox,editSubclass,1,0);
    } else if (std::wcscmp(cls,L"EdgeManager") == 0) {
      control(hwnd,L"BUTTON",L"New note",BS_PUSHBUTTON,NewNote,20,20,110,30);
      control(hwnd,L"BUTTON",L"All notes",BS_PUSHBUTTON,FilterAll,140,20,110,30);
      control(hwnd,L"BUTTON",L"On the deck",BS_PUSHBUTTON,FilterDeck,260,20,110,30);
      control(hwnd,L"BUTTON",L"Archive",BS_PUSHBUTTON,FilterArchive,380,20,110,30);
      searchBox=control(hwnd,L"EDIT",L"",ES_AUTOHSCROLL|WS_BORDER,4,20,65,620,30);
      SendMessageW(searchBox,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Search all notes"));
      listBox=control(hwnd,L"LISTBOX",L"",LBS_NOTIFY|WS_VSCROLL|LBS_NOINTEGRALHEIGHT,5,20,110,620,380);
    }
    return 0;
  }
  switch(msg) {
    case WM_GETMINMAXINFO: if(hwnd==editorWindow || hwnd==managerWindow) { auto* limits=reinterpret_cast<MINMAXINFO*>(lp); limits->ptMinTrackSize.x=510; limits->ptMinTrackSize.y=350; } return 0;
    case WM_SIZE: if (hwnd==editorWindow) { int w=LOWORD(lp),h=HIWORD(lp); MoveWindow(titleBox,20,12,w-40,30,TRUE); MoveWindow(richEdit,20,90,w-40,std::max(40,h-135),TRUE); MoveWindow(statusBox,20,h-32,w-40,24,TRUE); } else if(hwnd==managerWindow) { int w=LOWORD(lp),h=HIWORD(lp); MoveWindow(searchBox,20,65,w-40,30,TRUE); MoveWindow(listBox,20,110,w-40,std::max(30,h-130),TRUE); } return 0;
    case WM_PAINT: if(hwnd==dock) { drawDock(hwnd); return 0; } break;
    case WM_ERASEBKGND: { RECT r; GetClientRect(hwnd,&r); if(hwnd==editorWindow && active()) { auto brush=CreateSolidBrush(Paper[active()->color%8]); FillRect(reinterpret_cast<HDC>(wp),&r,brush); DeleteObject(brush); } else FillRect(reinterpret_cast<HDC>(wp),&r,nightBrush); return 1; }
    case WM_CTLCOLOREDIT: case WM_CTLCOLORSTATIC: case WM_CTLCOLORLISTBOX: { HDC dc=reinterpret_cast<HDC>(wp); SetBkMode(dc,TRANSPARENT); if(hwnd==editorWindow && active()) { SetTextColor(dc,Ink); SetBkColor(dc,Paper[active()->color%8]); return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH)); } SetTextColor(dc,NightText); SetBkColor(dc,Night); return reinterpret_cast<LRESULT>(nightBrush); }
    case WM_COMMAND: if(hwnd==editorWindow && HIWORD(wp)==EN_CHANGE) saveEditor(); else if(hwnd==managerWindow && LOWORD(wp)==4 && HIWORD(wp)==EN_CHANGE) refreshList(); else if(hwnd==managerWindow && LOWORD(wp)==5 && HIWORD(wp)==LBN_DBLCLK) { auto row=SendMessageW(listBox,LB_GETCURSEL,0,0); if(row>=0 && static_cast<std::size_t>(row)<rows.size()) { activeId=rows[row]; ShowWindow(managerWindow,SW_HIDE); setState(DeckState::Editing); } } else if(HIWORD(wp)==BN_CLICKED) command(LOWORD(wp)); return 0;
    case WM_MOUSEMOVE: if(hwnd==dock) { if(state==DeckState::Collapsed) setState(DeckState::Peek); TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0}; TrackMouseEvent(&track); } return 0;
    case WM_MOUSELEAVE: if(hwnd==dock && state==DeckState::Peek) setState(DeckState::Collapsed); return 0;
    case WM_LBUTTONUP: if(hwnd==dock) { int row=(GET_Y_LPARAM(lp)-6)/82,shown=0; for(const auto& n:store.notes) { if(n.archived || shown>=5) continue; if(shown++==row) { activeId=n.id; setState(DeckState::Editing); return 0; } } command(NewNote); } return 0;
    case WM_RBUTTONUP: menu(hwnd); return 0;
    case WM_ACTIVATE: if(hwnd==editorWindow && !modalOpen && LOWORD(wp)==WA_INACTIVE && reinterpret_cast<HWND>(lp)!=managerWindow) setState(DeckState::Collapsed); return 0;
    case WM_HOTKEY: if(wp==1) command(NewNote); else if(wp==2) command(AllNotes); else if(wp==3) command(ArchiveView); else setState(state==DeckState::Collapsed?DeckState::Peek:DeckState::Collapsed); return 0;
    case TrayMessage: if(lp==WM_RBUTTONUP) menu(dock); else if(lp==WM_LBUTTONUP) command(AllNotes); return 0;
    case WM_DISPLAYCHANGE: case WM_SETTINGCHANGE: if(hwnd==dock) setState(state); return 0;
    case WM_CLOSE: if(hwnd==editorWindow) setState(DeckState::Collapsed); else if(hwnd==managerWindow) ShowWindow(hwnd,SW_HIDE); else DestroyWindow(hwnd); return 0;
    case WM_DESTROY: if(hwnd==dock) { NOTIFYICONDATAW ni{}; ni.cbSize=sizeof(ni); ni.hWnd=dock; ni.uID=1; Shell_NotifyIconW(NIM_DELETE,&ni); for(int i=1;i<=4;++i) UnregisterHotKey(dock,i); PostQuitMessage(0); } return 0;
  }
  return DefWindowProcW(hwnd,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR,int) {
  instance=inst; SetProcessDPIAware();
  HANDLE singleton=CreateMutexW(nullptr,FALSE,L"Local\\EdgeNotesNativeWindows"); if(GetLastError()==ERROR_ALREADY_EXISTS) { if(singleton) CloseHandle(singleton); return 0; }
  richLibrary=LoadLibraryW(L"Msftedit.dll"); if(!richLibrary) return 1;
  INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
  uiFont=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
  handFont=CreateFontW(-20,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe Print"); nightBrush=CreateSolidBrush(Night);
  PWSTR local=nullptr; if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))) return 1;
  std::wstring dir=std::wstring(local)+L"\\EdgeNotes"; CoTaskMemFree(local); CreateDirectoryW(dir.c_str(),nullptr); storagePath=dir+L"\\notes.enotes";
  HANDLE file=CreateFileW(storagePath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file!=INVALID_HANDLE_VALUE) { LARGE_INTEGER size{}; if(GetFileSizeEx(file,&size) && size.QuadPart>0 && size.QuadPart<128*1024*1024) { std::string bytes(static_cast<std::size_t>(size.QuadPart),'\0'); DWORD read=0; if(ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) && read==bytes.size()) { try { store.notes=edgenotes::decode(bytes); } catch(...) { MessageBoxW(nullptr,L"The notes file could not be read. It has not been overwritten.",L"EdgeNotes",MB_ICONERROR); CloseHandle(file); return 2; } } else { CloseHandle(file); return 2; } } else { CloseHandle(file); return 2; } CloseHandle(file); }
  else if(GetLastError()!=ERROR_FILE_NOT_FOUND) { MessageBoxW(nullptr,L"Your notes file could not be opened. EdgeNotes will not overwrite it.",L"EdgeNotes",MB_ICONERROR); return 2; }
  for(const auto& n:store.notes) nextId=std::max(nextId,n.id+1);
  if(store.notes.empty()) { store.notes.push_back({nextId++,"A passing thought","A fresh page, just for you.","",0,false}); }
  activeId=store.notes.front().id;
  for(auto cls:{L"EdgeDock",L"EdgeEditor",L"EdgeManager"}) { WNDCLASSW wc{}; wc.lpfnWndProc=wndProc; wc.hInstance=inst; wc.lpszClassName=cls; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION); RegisterClassW(&wc); }
  dock=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"EdgeDock",L"EdgeNotes deck",WS_POPUP,0,0,34,540,nullptr,nullptr,inst,nullptr);
  editorWindow=CreateWindowExW(WS_EX_TOOLWINDOW,L"EdgeEditor",L"EdgeNotes",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME,100,100,510,540,nullptr,nullptr,inst,nullptr);
  managerWindow=CreateWindowExW(0,L"EdgeManager",L"EdgeNotes - All notes",WS_OVERLAPPEDWINDOW,140,120,680,560,nullptr,nullptr,inst,nullptr);
  if(!dock||!editorWindow||!managerWindow||!richEdit) return 3;
  trayIcon=LoadIconW(nullptr,IDI_APPLICATION); NOTIFYICONDATAW ni{}; ni.cbSize=sizeof(ni); ni.hWnd=dock; ni.uID=1; ni.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP; ni.uCallbackMessage=TrayMessage; ni.hIcon=trayIcon; lstrcpyW(ni.szTip,L"EdgeNotes - local notes"); Shell_NotifyIconW(NIM_ADD,&ni);
  bool keys=true; keys=RegisterHotKey(dock,1,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'N')!=0 && keys; keys=RegisterHotKey(dock,2,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'A')!=0 && keys; keys=RegisterHotKey(dock,3,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'L')!=0 && keys;
  RegisterHotKey(dock,4,MOD_ALT|MOD_NOREPEAT,VK_SPACE);
  if(!keys) MessageBoxW(nullptr,L"One or more shortcuts are already in use. EdgeNotes remains available from the tray.",L"EdgeNotes",MB_ICONINFORMATION);
  setState(DeckState::Collapsed);
  MSG message{}; int result; while((result=GetMessageW(&message,nullptr,0,0))>0) { TranslateMessage(&message); DispatchMessageW(&message); }
  if(IsWindow(editorWindow)) DestroyWindow(editorWindow);
  if(IsWindow(managerWindow)) DestroyWindow(managerWindow);
  DeleteObject(uiFont); DeleteObject(handFont); DeleteObject(nightBrush); FreeLibrary(richLibrary); if(singleton) CloseHandle(singleton); return result<0?1:0;
}