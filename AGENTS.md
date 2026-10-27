<!-- LOVABLE:BEGIN -->

> [!IMPORTANT]
> This project is connected to [Lovable](https://lovable.dev). Avoid rewriting
> published git history — force pushing, or rebasing/amending/squashing commits
> that are already pushed — as it rewrites history on Lovable's side and the
> user will likely lose their project history.
>
> Commits you push to the connected branch sync back to Lovable and show up in
> the editor, so keep the branch in a working state.

<!-- LOVABLE:END -->

- Keep EdgeNotes as a browser-local desktop simulation with storage hydrated after mount; this preserves offline notes without hydration mismatches.
- Use TipTap for rich editing and Markdown serialization, and keep note data and editor behavior in separate modules for maintainability.
- Keep the browser preview and standalone Windows implementation independent; native Windows code lives under `native/windows` and uses Win32/RichEdit without a browser runtime.
- Drive native work through Windows messages and user actions, and expire deletion undo by elapsed time on the next interaction rather than periodic timers; this keeps the idle message loop blocked.
- Bundle handwriting fonts locally and implement preview deck states with CSS transitions; this avoids font network dependencies and continuous animation loops.
- Keep browser dock geometry in a pure positioning module, with measured dock size and viewport bounds; this keeps four-edge dragging testable and prevents offscreen placement.
- Clip desktop overflow and focus new-note titles without scrolling; hidden sliding surfaces must never scroll the desktop or push the editor offscreen.
