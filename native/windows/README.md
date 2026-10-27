# EdgeNotes native Windows source

This is a standalone Win32 C++ desktop implementation, **not the browser preview, Electron, a PWA, or a WebView**. It uses Windows RichEdit and GDI+, system fonts, a blocking Windows message loop, local files, a tray icon, and registered global shortcuts. It contains no network client, cloud synchronization, analytics, telemetry, polling, `SetTimer`, or periodic task.

## Build and Run

To build locally, open Developer PowerShell (VS 2022 Build Tools) and run:

```powershell
./native/windows/build.ps1
```

Output:

- Tests: `native/windows/build/store-tests.exe`
- App: `native/windows/build/EdgeNotes.exe`
- Release executable: `release/EdgeNotes-windows-x64.exe`
- Portable release package: `release/EdgeNotes-windows-x64.zip`
- Bundled font: `release/excalifont-regular.ttf`

## Features & Redesign Parity

1. **Edge Dock**:
   - Slim pill tabs for the first 5 deck notes with rotated vertical titles (`Segoe UI Bold`).
   - `+` quick-add note button with hover state.
   - 6-dot drag grip handle (`:::`) supporting dragging to any screen edge (Left, Right, Top, Bottom) with automatic edge snapping and persistence in `%LOCALAPPDATA%\EdgeNotes\config.ini`.
   - Multi-monitor work area detection and Per-Monitor V2 DPI scaling.

2. **3 Connected States with Smooth Transitions**:
   - **Collapsed**: Slim pastel pill tabs resting on the desktop screen edge.
   - **Peek Deck**: Clean dark panel (`#1b1c1e`) showing pastel preview cards with note snippets, thought counter, and quick-action buttons. Non-overlapping deck layout. Click outside or press `Escape` to close.
   - **Editor**: Full note editing sheet matching the note's pastel color.

3. **Editor**:
   - Breadcrumb navigation: `← EdgeNotes › [Title]`, pin button, and close button.
   - Large bold title input with auto-select and 200-character limit.
   - Handwriting body text in `Segoe Print`.
   - Modern flat toolbar pill:
     - Text styling: Bold (`Ctrl+B`), Italic (`Ctrl+I`), H1, H2, Bullet list, Checklist (`[] `).
     - Live auto-formatting triggers (`# `, `## `, `### `, `- `, `* `, `[] `).
     - Heading reset on Enter key.
     - 8 pastel color palette swatches (Mint, Sky, Lavender, Yellow, Rose, Peach, Aqua, Paper) with active indicator ring.
     - Export to RTF/Text, Archive, and Delete.
   - Hidden scrollbars (wheel scrollable).
   - Auto-save on typing.

4. **All-Notes Manager**:
   - Dark theme (`#1b1c1e` / `#24262a` / `#383a40`) matching the Lovable browser app.
   - 3-Column layout:
     - Column 1: `+ New note` mint button, filter navigation (`All notes`, `On the deck`, `Archive`) with live count badges.
     - Column 2: Instant search box (`Find a thought...`), note cards with colored indicator stripes and titles.
     - Column 3: Status indicator (`● ON THE DECK` / `● ARCHIVED`), full paper sheet preview with Segoe Print body, and `Open note ↗` button.
   - Double-click any note to open directly in Editor.

5. **Safety, Shortcuts & Persistence**:
   - 10-second undo deletion via `GetTickCount64()` (zero background timers).
   - Global shortcuts: `Ctrl+Alt+N` (New note), `Ctrl+Alt+A` (All notes), `Ctrl+Alt+L` (Toggle dock), `Alt+Space` (Toggle peek/deck).
   - Non-blocking notification if a shortcut is claimed by another application.
   - System tray menu with Explorer restart recovery (`TaskbarCreated`).
   - Single-instance activation (`EdgeNotes_ActivateInstance`).
   - 0% idle CPU and native memory footprint (~5-30 MB working set).
