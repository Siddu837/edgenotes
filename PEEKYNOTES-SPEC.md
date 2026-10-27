# Reverse-Engineered Architecture & Feature Specification: PeekyNotes -> EdgeNotes

Extracted via **REA (Reverse Engineering Assistant)** from `C:\Users\keert\Downloads\PeekyNotes.exe`.

---

## 1. Executive Summary & Binary Profile

| Property                | Details                                                                           |
| ----------------------- | --------------------------------------------------------------------------------- |
| **Original Binary**     | `C:\Users\keert\Downloads\PeekyNotes.exe` (26,518,325 bytes)                      |
| **SHA-256**             | `64081df9d5418eb3546592140fc1ec2873c94c24ec7c595992f845f6bbc54bb4`                |
| **Packaging**           | .NET 8 Single-File Host (`PublishSingleFile=true`) bundling 6 files               |
| **Bundled Core Logic**  | `PeekyNotes.Core.dll` (101,376 bytes), .NET 8.0 AnyCPU CLI                        |
| **DPI Awareness**       | `PerMonitorV2` (manifested)                                                       |
| **Target App Name**     | EdgeNotes (`C:\Users\keert\.gemini\antigravity\scratch\edgenotes`)                |
| **Implementation Rule** | High-performance native Win32 C++ in `native/windows/` (no Electron, no WebView2) |

---

## 2. Reverse-Engineered Data Schema

From `PeekyNotes.Model`:

### `Note`

```json
{
  "id": "guid-or-string",
  "title": "Untitled note",
  "body": "Empty note",
  "colorIndex": 0,
  "railLabel": "N",
  "displayTitle": "Untitled note",
  "order": 0,
  "isInDeck": true,
  "archived": false,
  "completedTasksAtBottom": true,
  "font": 0,
  "blocks": [
    {
      "line": 0,
      "kind": 0,
      "done": false,
      "isPlain": true
    }
  ],
  "spans": [
    {
      "start": 0,
      "length": 0,
      "bold": false,
      "italic": false,
      "underline": false,
      "strikethrough": false,
      "link": ""
    }
  ],
  "updatedUtc": "2026-10-20T12:00:00Z"
}
```

### Enumerations & Models

- **`BlockKind`**:
  - `0`: Plain Text
  - `1`: Heading (`# `)
  - `2`: Bullet List (`- ` or `• `)
  - `3`: Numbered List (`1. ` or `. `)
  - `4`: Task / Checklist (`- [ ] ` uncompleted, `- [x] ` completed)
- **`NoteFont`**:
  - `0`: Handwritten (`Kalam` / Segoe Script)
  - `1`: Sans-serif (`Segoe UI`)
  - `2`: Rounded
  - `3`: Monospace / Typewriter (`Consolas` / `Cascadia Code`)
- **`ColorIndex`** (8 Pastel Deck Colors):
  1. `0` Butter (Yellow): `#FDE047` / `#FDE68A`
  2. `1` Peach (Orange): `#FB923C` / `#FCD34D`
  3. `2` Blush (Pink): `#F472B6` / `#FB7185`
  4. `3` Lilac (Purple): `#A855F7` / `#C084FC`
  5. `4` Mint (Green): `#6EE7B7` / `#34D399`
  6. `5` Sky (Cyan): `#22D3EE` / `#38BDF8`
  7. `6` Blue (Indigo): `#60A5FA` / `#818CF8`
  8. `7` Slate / Lime: `#A3E635` / `#BEF264`

---

## 3. Storage & Persistence Engine

From `PeekyNotes.Model.NoteRepository`:

- **Active Directory**: `%USERPROFILE%\Documents\Peeky Notes\`
- **Database File**: `notes.json`
- **Fallback / Legacy**: `%LOCALAPPDATA%\PeekyNotes\` (auto-migrated with `.moved-to-documents.txt`)
- **Atomic Write Workflow**:
  1. Write to `.tmp` file
  2. Verify payload integrity
  3. Copy previous valid state to `.bak`
  4. Atomically replace `notes.json` (`MoveFileEx` with `MOVEFILE_REPLACE_EXISTING`)
  5. If corruption is detected, preserve as `.corrupt-yyyyMMdd-HHmmss` and recover from `.bak`.
- **Exporting**:
  - Export single note as `.md` (Markdown format)
  - Export all notes as `Peeky Notes backup yyyy-MM-dd HHmm.json`

---

## 4. Window & Shell Interactions

- **Window Styles**: Borderless, top-most tool window (`WS_EX_TOOLWINDOW | WS_EX_TOPMOST`), avoiding taskbar crowding and Alt+Tab clutter.
- **Edge Docking Mechanism**:
  - Attached to Left, Right, Top, or Bottom screen edges.
  - Multi-monitor awareness (tracks primary or active display geometry and work area via `MonitorFromWindow` / `GetMonitorInfo`).
  - Hides or yields when a fullscreen app or game is active (`SHAppBarMessage` / fullscreen window detection).
- **Three-Tier Peek UX**:
  1. **Collapsed Rail/Dock**: Slim vertical tabs pinned to the screen border displaying `RailLabel`.
  2. **Hover / Peek Deck**: Hovering over a tab unfolds a fanned stack/preview of cards without opening the full editor.
  3. **Floating Note Editor**: Clicking an item pops open the active note. Click-outside or pressing `Escape` dismisses back to edge.
- **Global Hotkeys**:
  - `Ctrl + Alt + N`: Create and focus new note
  - `Ctrl + Alt + A`: Open All Notes Manager / Search Palette
  - `Alt + Space` / `Ctrl + Alt + L`: Toggle Edge Dock visibility
  - `Ctrl + .`: Cycle note color through the 8 pastel shades
- **Deletion UX**:
  - Clicking delete does not prompt an intrusive confirmation dialog.
  - Immediately hides the note and initiates a 10-second non-blocking undo countdown banner with an **Undo** button.
  - Commit deletion only after 10 seconds elapse or on application exit.
- **System Tray Icon**:
  - Context menu: _New Note_, _All Notes_, _Dock Position (Left/Right/Top/Bottom)_, _Settings_, _Exit_.
  - Listens for `TaskbarCreated` message to restore tray icon if `explorer.exe` restarts.

---

## 5. EdgeNotes Implementation Objectives

The target codebase in `C:\Users\keert\.gemini\antigravity\scratch\edgenotes`:

- Native Win32 C++ implementation in `native/windows/` (`main.cpp`, `note-store.hpp`, `config.hpp`, `theme.hpp`).
- Build system: `build.ps1` using MSVC `cl.exe` + `rc.exe`.
- Requirement: Zero CPU usage when idle, ~7 MB working set, zero external runtime dependencies.
- Update `main.cpp` and `note-store.hpp` to match this exact feature matrix and visual polish.
