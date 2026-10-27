# Edge Notes Drawer

Build the EdgeNotes Core Desktop App experience with the complete edge-docked notes interface.

Key features to implement:

1. Edge-Docked Drawer Layout:
   - A vertical dock bar on the edge (dockable left or right) with color-coded deck categories (e.g., Work, Personal, Scratchpad, Archive, Ideas).
   - Smooth slide-out / slide-in drawer mechanism with a pin/unpin toggle button (pinned = keeps open; unpinned = closes when clicking outside or pressing Escape).
   - Global keyboard shortcut handler (simulating Alt+Space or Win+Alt+N to toggle peek drawer).

2. TipTap / Lexical Markdown & Rich Editor:
   - Floating/fixed formatting toolbar (Headings H1-H3, Bold, Italic, Strikethrough, Code blocks, Bullet lists, Interactive Checklists, Links).
   - Instant typing with auto-save to browser local persistence (simulating local JSON atomic saves).
   - Support for code snippets with copy-to-clipboard button.
   - Interactive checklist items with strike-through completion.

3. Deck & Note Management:
   - Switch between multiple colored decks/tabs with instant transition.
   - Add new note within a deck, delete note, duplicate, and rename.
   - Tagging system (e.g. #urgent, #todo, #idea) with quick tag filter.
   - Fast full-text search across all notes in all decks (title and body).

4. Clean Desktop-First Aesthetic:
   - Sleek modern dark & light mode with subtle borders, glassmorphism/translucency effect, and compact typography suited for an unobtrusive side drawer.
   - Quick export option: Export active note or all notes as Markdown (.md) or plain text (.txt).
   - Status bar at the bottom showing word count, character count, and 'Saved locally' indicator with timestamp.

This project was built with [Lovable](https://lovable.dev).

**Live app**: https://edge-notes-drawer.lovable.app

## Build with Lovable

Continue developing this project in the [Lovable editor](https://lovable.dev/projects/c27c528e-96eb-4f11-9359-7244a6649303).

- **Ship faster**: describe what you want to build and Lovable handles the code.
- **Stay in sync**: every change made in Lovable is committed straight to this repository.
- **Full ownership**: this code is yours. Push to `main` on GitHub and your changes sync back into Lovable, ready for your next prompt.

## Development

Prefer working locally? You need Node.js and npm — [install with nvm](https://github.com/nvm-sh/nvm#installing-and-updating).

```sh
git clone <this-repository-url>
cd <repository-name>
npm i
npm run dev
```
