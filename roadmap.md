# EdgeNotes

- [x] Fix overlapping previews, outside-click dismissal, and new-note visibility in the browser.
- [x] Add four-edge docking and drag-to-position controls; verify interactions.
- [x] Build dock, drawer, deck list, search, and note management.
- [x] Add rich editing, local persistence, tags, and exports.
- [x] Verify editing, persistence, drawer controls, and responsive layout.
- [x] Redesign the browser preview with three-state edge deck, pastel sheets, handwriting, auto-formatting, deletion undo, and dark all-notes manager.
- [x] Prepare standalone native Windows desktop source and GitHub Actions build workflow (not Electron or PWA).
- [x] Verify browser interactions and tests; document Windows build and runtime verification limitations. Native storage tests passed and Windows x64 source cross-compiled successfully.
- [ ] Run GitHub Actions packaging and complete native Windows runtime testing — blocked: requires a connected GitHub repository/Windows runner and Windows test machine.
- [ ] Verify 7 MB working set with ten notes at 25 seconds, 0% idle CPU, and no uploads/cloud sync/telemetry/analytics — blocked: requires Windows runtime measurement and audit; no polling or timers appear in native application source.
- [ ] Push the project to GitHub and give Windows download/build instructions.
- [ ] macOS delivery — deferred by the user until after Windows.
