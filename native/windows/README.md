# EdgeNotes native Windows source

This is a standalone Win32 C++ desktop implementation, **not the browser preview, Electron, a PWA, or a WebView**. It uses Windows RichEdit and GDI, system fonts, a blocking Windows message loop, local files, a tray icon, and registered global shortcuts. It contains no network client, cloud synchronization, analytics, telemetry, polling, `SetTimer`, or periodic task.

## Build and download

Connect this project to GitHub through Lovable's GitHub connection. The included **Build native EdgeNotes for Windows** workflow compiles x64 and x86 on Windows, runs native storage tests, and uploads portable ZIP artifacts. Open the workflow run in GitHub Actions and download the corresponding artifact. Workflow execution is not performed by editing this project; a GitHub repository and runner are required.

For a local build, install Visual Studio 2022 Build Tools with **Desktop development with C++** and a Windows SDK. Open a Developer PowerShell and run `./native/windows/build.ps1`. Output: `native/windows/build/EdgeNotes.exe`. No Node installation is required. This is a portable executable, not an installer. The executable is unsigned, so Windows SmartScreen may warn; production distribution requires code signing and a reviewed installer.

## Current implementation

- Collapsed edge tabs, hover-expanded peek deck, and a separate editing window; left/right docking via the tray menu.
- Pastel note surfaces, Segoe Print handwriting, title editing, RichEdit body, bold/italic/headings/bullets, and heading/bullet prefix triggers (`# `, `## `, `### `, `- `, `* `).
- Dark searchable all-notes manager; double-click a result to edit. All notes, on-deck, and archive filters.
- Add notes, archive/restore, cycle eight paper colors, delete and undo within ten seconds.
- Global Ctrl+Alt+N / A / L for new note / manager / archive; Alt+Space toggles the deck if the shortcut is available. Shortcut collisions are reported; the tray remains available.
- Ctrl+B/I formatting, Ctrl+Backspace deletion, Ctrl+period paper color, and Escape collapse while editing. Ctrl+Z restores the latest deletion while undo is eligible; otherwise RichEdit's regular undo applies.
- RTF formatting and UTF-8 title/body persisted to `%LOCALAPPDATA%\EdgeNotes\notes.enotes` through temp-file writes, flushing, and atomic replacement. Corrupt or unreadable data is not overwritten. Plain-text and RTF export.
- A brief OS-controlled editor fade on opening, disabled when Windows client-area animation is disabled. No idle animation loop.

Undo eligibility is checked against `GetTickCount64()` on user actions. It becomes invalid after ten seconds without a timer. Its in-memory record is discarded on the next command. Deletion is immediately persisted; recovery does not survive quitting or restarting. The manager is native standard controls, not a rendering of the web design.

## Not yet native feature parity

This source is a native foundation, not a fully verified release. Browser TipTap checklist interaction, fenced-code copying, Markdown export, tags/deck categories, advanced auto-formatting, multi-monitor/DPI migration, full-system theme following, polished manager preview, fullscreen-aware hiding, Windows startup, and exact stacked-sheet animations still require native implementation and Windows QA. The native deck displays the first five non-archived notes; the manager provides access to every note.

The browser preview preserves its independent browser-local data; no automated import to the native store is provided. Use exports for manual transfer. A portable build does not remove notes when its executable is deleted.

## Verification and acceptance gates

The source includes tests for serialization, Unicode round-trips, rich-text retention, invalid-file rejection, note ordering, and ten-second undo expiry. CI compiles and tests on Windows; GUI interactions and performance are **not automatically proven** by compilation.
The native storage tests have passed in the preparation environment, and the full Windows GUI source has cross-compiled and linked to a PE x64 executable with MinGW. The MSVC GitHub Actions workflow has not been run here; this is not a Windows runtime or performance verification.

Before release, on an actual Windows desktop:

1. Build both architectures and verify launch, tray restoration, keyboard collisions, docking both sides, all three states, formatting, searching, archive/restore, export, restart persistence, and undo just before and after ten seconds.
2. Create ten real notes. Wait 25 seconds after launch and record private working set in Task Manager/Process Explorer. **7 MB is a required target, not a measured result or guarantee.** Do not trim working sets to manufacture a measurement.
3. Measure idle CPU over a meaningful observation interval with WPR/Process Explorer; the required 0% idle CPU is unverified. Inspect wakeups; no timers or polling should be introduced.
4. Trace network activity with Windows tools and verify no uploads. Review the binary and dependencies. This native source has no networking, but an independent runtime audit remains necessary.
5. Test Windows 10/11, multiple displays and scaling levels, reduced motion, suspend/resume, Explorer restart, file-permission failures, disk full, and crash recovery. Explorer-restart tray recovery is not yet implemented.

macOS is intentionally deferred until the Windows implementation is accepted.