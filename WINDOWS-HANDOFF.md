# EdgeNotes for Windows — handoff brief for a coding agent (Antigravity, Cursor, etc.)

Read this file fully before changing code. You are working on a real Windows PC, so you can build, run, click and fix.

## Goal

Make the **native Windows app** in `native/windows/` look and behave like the redesigned browser version in `src/`
(the browser version is the visual and behaviour reference). Every button, shortcut and window must work like a proper desktop app.

## Hard rules

- Native Win32 C++ only. **No Electron, no WebView2, no PWA, no browser runtime.** (Direct2D/DirectWrite, GDI, RichEdit, and the Windows SDK are fine.)
- No network code, telemetry, cloud sync, polling, or periodic timers (`SetTimer`, timer threads). Work happens only in response to Windows messages and user actions. Animations may run only while they are visibly playing and must stop afterwards.
- Target: ~7 MB private working set with ten notes, 0% CPU when idle.
- Portable app: single `.exe`, notes saved to `%LOCALAPPDATA%\EdgeNotes\notes.enotes` (keep the existing atomic-write store in `note-store.hpp`).
- No sample/mock notes on first launch. No visible scrollbars (scrolling with the wheel still works).
- Keep the browser app in `src/` unchanged unless asked; the two are independent.
- Do not force-push or rewrite git history on `main` (this repo syncs with Lovable).

## Reference design (see `src/routes/index.tsx`, `src/styles.css`, `src/lib/notes.ts`, `src/lib/dock-position.ts`)

- **Edge dock:** slim pill tabs on a screen edge, one per note (first five non-archived), in eight pastel paper colors, plus a **+** tab. Draggable to the left, right, top or bottom edge; position remembered. Must never go off-screen; respect multiple monitors and DPI scaling.
- **Three states:** collapsed tabs → hover/click shows a fanned "peek deck" of note sheets that do not overlap → click a sheet opens the editor. Clicking outside or pressing Escape closes it. Smooth slide/fade animations.
- **Editor window:** pastel sheet, large bold **title** at top (cursor starts there on a new note), handwriting-style body font, toolbar with bold, italic, headings, bullets, checklist, color, archive, delete. Auto-format triggers: `# `, `## `, `### `, `- `, `* `, `[] `. Auto-save while typing.
- **All-notes manager (dark):** search box, filters (All / On deck / Archive), list with preview, open/restore/delete.
- **Delete** with a 10-second undo (checked with `GetTickCount64()` on the next user action, no timer).
- **Shortcuts:** Ctrl+Alt+N new note, Ctrl+Alt+A all notes, Ctrl+Alt+L show/hide dock, Alt+Space toggle dock. Report collisions instead of failing silently.
- **Tray icon:** New note, All notes, Dock left/right/top/bottom, Quit. Restore the icon after Explorer restarts (`TaskbarCreated`).
- **Theme:** follow Windows light/dark mode.

## Known problems reported by the user

- The app still looks like the old, plain design (classic raised buttons, basic windows) instead of the redesign.
- Title, create note, and other buttons/features do not behave properly. Test every control by hand.

## Build and test

1. Install Visual Studio 2022 (or Build Tools) with **Desktop development with C++**.
2. Open **Developer PowerShell for VS 2022** in the repo folder and run `./native/windows/build.ps1`.
3. Run `native/windows/build/EdgeNotes.exe`. Quit any older copy from the tray first (or Task Manager → End task), otherwise you are testing the old version.
4. Storage tests: `native/windows/store-tests.cpp` (also run by `.github/workflows/windows-native.yml`).
5. After every fix: rebuild, click through every button and shortcut above, restart the app to check notes persist.
6. When done, copy the new build to `release/EdgeNotes-windows-x64.exe` and push so the download link stays current.
