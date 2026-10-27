import { createFileRoute } from "@tanstack/react-router";
import { useEffect, useRef, useState } from "react";
import {
  Archive,
  ArrowLeft,
  ArrowRight,
  ArrowUpRight,
  ArrowUp,
  ArrowDown,
  Grip,
  BriefcaseBusiness,
  Check,
  ChevronDown,
  ChevronLeft,
  ChevronsRight,
  Copy,
  Download,
  FileText,
  Hash,
  Layers2,
  Lightbulb,
  Minus,
  Moon,
  MoreHorizontal,
  Pin,
  PinOff,
  Plus,
  Search,
  Settings2,
  Sparkles,
  Sun,
  Trash2,
  UserRound,
  X,
  Wifi,
  BatteryFull,
  Leaf,
  Pencil,
  Grid2X2,
  Undo2,
} from "lucide-react";
import { Button } from "@/components/ui/button";
import {
  DropdownMenu,
  DropdownMenuTrigger,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuSeparator,
  DropdownMenuLabel,
} from "@/components/ui/dropdown-menu";
import { NoteEditor } from "@/components/note-editor";
import {
  decks,
  initialNotes,
  removeLegacySamples,
  plainText,
  STORAGE_KEY,
  noteColor,
  noteColors,
  getNoteFontClass,
  type Note,
  type NoteFont,
} from "@/lib/notes";
import { NotesManager } from "@/components/notes-manager";
import { SettingsDialog } from "@/components/settings-dialog";
import wallpaper from "@/assets/night-lake.jpg";
import { toMarkdown } from "@/lib/markdown-export";
import { dockAtPoint, dockCoordinates, type DockSide } from "@/lib/dock-position";

export const Route = createFileRoute("/")({
  head: () => ({
    meta: [
      { title: "EdgeNotes — A little space for your thoughts" },
      {
        name: "description",
        content:
          "Your quiet, edge-docked workspace. Capture ideas, organize notes, and write beautifully with private local saving.",
      },
      { property: "og:title", content: "EdgeNotes — A little space for your thoughts" },
      {
        property: "og:description",
        content:
          "A thoughtful desktop notes workspace with rich editing, colored decks, and local autosave.",
      },
      { property: "og:type", content: "website" },
      { name: "twitter:card", content: "summary_large_image" },
    ],
  }),
  component: Index,
});

const deckIcons = [BriefcaseBusiness, UserRound, FileText, Lightbulb, Archive];
function Index() {
  const [notes, setNotes] = useState<Note[]>(initialNotes);
  const [deck, setDeck] = useState("work");
  const [activeId, setActiveId] = useState("");
  const [open, setOpen] = useState(true);
  const [pinned, setPinned] = useState(false);
  const [peeking, setPeeking] = useState(true);
  const [manager, setManager] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [defaultFont, setDefaultFont] = useState<NoteFont>("handwritten");
  const [completedTasksAtBottom, setCompletedTasksAtBottom] = useState(true);
  const [deleted, setDeleted] = useState<{ note: Note; index: number; expires: number } | null>(
    null,
  );
  const [side, setSide] = useState<DockSide>("right");
  const [dockPosition, setDockPosition] = useState(0.5);
  const [dragging, setDragging] = useState(false);
  const [viewport, setViewport] = useState({ width: 1068, height: 639 });
  const [dockSize, setDockSize] = useState({ width: 35, height: 480 });
  const [dark, setDark] = useState(false);
  const [query, setQuery] = useState("");
  const [tag, setTag] = useState("");
  const [ready, setReady] = useState(false);
  const [saveStatus, setSaveStatus] = useState("Saved locally");
  const [toast, setToast] = useState("");
  const [mobileList, setMobileList] = useState(false);
  const [tagInput, setTagInput] = useState(false);
  const search = useRef<HTMLInputElement>(null);
  const titleInput = useRef<HTMLInputElement>(null);
  const drawer = useRef<HTMLDivElement>(null);
  const dock = useRef<HTMLElement>(null);
  const peek = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const resize = () => setViewport({ width: window.innerWidth, height: window.innerHeight });
    resize();
    window.addEventListener("resize", resize);
    const element = dock.current;
    const observer = new ResizeObserver(() => {
      if (element) setDockSize({ width: element.offsetWidth, height: element.offsetHeight });
    });
    if (element) observer.observe(element);
    return () => {
      window.removeEventListener("resize", resize);
      observer.disconnect();
    };
  }, []);
  const moveDock = (x: number, y: number) => {
    const placement = dockAtPoint(x, y, viewport.width, viewport.height);
    setSide(placement.side);
    setDockPosition(placement.position);
  };
  const chooseSide = (next: DockSide) => {
    setSide(next);
    setDockPosition(0.5);
  };
  const active = notes.find((n) => n.id === activeId);
  const currentDeck = decks.find((d) => d.id === deck) || {
    id: "work",
    name: "Work",
    color: "work",
    icon: "briefcase",
  };
  const activeDeck = decks.find((d) => d.id === active?.deck) || currentDeck;
  const visible = notes.filter(
    (n) =>
      (query
        ? `${n.title} ${plainText(n.content)} ${n.tags.join(" ")}`
            .toLowerCase()
            .includes(query.toLowerCase())
        : n.deck === deck) &&
      (!tag || n.tags.includes(tag)),
  );
  const allTags = [
    ...new Set(notes.filter((n) => query || n.deck === deck).flatMap((n) => n.tags)),
  ];
  const text = active ? plainText(active.content) : "";
  const words = text.split(/\s+/).filter(Boolean).length;
  const showToast = (message: string) => {
    setToast(message);
  };
  useEffect(() => {
    if (!toast) return;
    const timer = setTimeout(() => setToast(""), 2500);
    return () => clearTimeout(timer);
  }, [toast]);
  useEffect(() => {
    if (!deleted) return;
    const timer = setTimeout(() => setDeleted(null), Math.max(0, deleted.expires - Date.now()));
    return () => clearTimeout(timer);
  }, [deleted]);
  useEffect(() => {
    try {
      const saved = localStorage.getItem(STORAGE_KEY);
      if (saved) {
        const value = JSON.parse(saved);
        if (
          Array.isArray(value.notes) &&
          value.notes.every(
            (n: Note) =>
              typeof n.id === "string" &&
              typeof n.content === "string" &&
              typeof n.title === "string" &&
              Array.isArray(n.tags),
          )
        ) {
          const restored = removeLegacySamples(value.notes);
          setNotes(restored);
          setActiveId(
            restored.some((n) => n.id === value.activeId) ? value.activeId : restored[0]?.id || "",
          );
          setDeck(value.deck || "work");
          setDark(Boolean(value.dark));
          setSide(["left", "right", "top", "bottom"].includes(value.side) ? value.side : "right");
          setDockPosition(
            typeof value.dockPosition === "number"
              ? Math.max(0, Math.min(1, value.dockPosition))
              : 0.5,
          );
          setPinned(value.pinned === true);
          if (
            value.defaultFont &&
            ["handwritten", "sans", "rounded", "mono"].includes(value.defaultFont)
          ) {
            setDefaultFont(value.defaultFont);
          }
          if (typeof value.completedTasksAtBottom === "boolean") {
            setCompletedTasksAtBottom(value.completedTasksAtBottom);
          }
        }
      }
    } catch {
      setSaveStatus("Could not read local notes");
    }
    setReady(true);
  }, []);
  useEffect(() => {
    if (!ready) return;
    setSaveStatus("Saving…");
    const timer = setTimeout(() => {
      try {
        localStorage.setItem(
          STORAGE_KEY,
          JSON.stringify({
            notes,
            activeId,
            deck,
            dark,
            side,
            dockPosition,
            pinned,
            defaultFont,
            completedTasksAtBottom,
          }),
        );
        setSaveStatus("Saved locally");
      } catch {
        setSaveStatus("Storage full — export your notes");
      }
    }, 350);
    return () => clearTimeout(timer);
  }, [
    notes,
    activeId,
    deck,
    dark,
    side,
    dockPosition,
    pinned,
    defaultFont,
    completedTasksAtBottom,
    ready,
  ]);
  useEffect(() => {
    const keydown = (e: KeyboardEvent) => {
      if (e.altKey && (e.code === "Space" || (e.metaKey && e.code === "KeyN"))) {
        e.preventDefault();
        setOpen((o) => !o);
        setPeeking(true);
      }
      if (e.key === "Escape") {
        setOpen(false);
        setManager(false);
        setSettingsOpen(false);
      }
      if (e.ctrlKey && e.altKey && e.code === "KeyA") {
        e.preventDefault();
        setManager(true);
      }
      if (e.ctrlKey && e.altKey && e.code === "KeyL") {
        e.preventDefault();
        switchDeck("archive");
        setManager(true);
      }
      if ((e.metaKey || e.ctrlKey) && e.key === ",") {
        e.preventDefault();
        setSettingsOpen(true);
      }
      if ((e.metaKey || e.ctrlKey) && e.key === "k") {
        e.preventDefault();
        setOpen(true);
        setPeeking(false);
        setMobileList(true);
        setTimeout(() => search.current?.focus(), 20);
      }
    };
    const outside = (e: PointerEvent) => {
      if (
        !pinned &&
        e.target instanceof Node &&
        !drawer.current?.contains(e.target) &&
        !dock.current?.contains(e.target) &&
        !peek.current?.contains(e.target) &&
        !(
          e.target instanceof Element &&
          e.target.closest('[role="menu"], .manager-overlay, .settings-overlay, .undo-toast')
        )
      )
        setOpen(false);
    };
    window.addEventListener("keydown", keydown);
    window.addEventListener("pointerdown", outside);
    return () => {
      window.removeEventListener("keydown", keydown);
      window.removeEventListener("pointerdown", outside);
    };
  }, [pinned, notes]);
  const update = (changes: Partial<Note>) =>
    setNotes((items) =>
      items.map((n) =>
        n.id === activeId ? { ...n, ...changes, updated: new Date().toISOString() } : n,
      ),
    );
  const switchDeck = (id: string) => {
    if (id === deck && open && peeking) {
      setOpen(false);
      return;
    }
    setDeck(id);
    setQuery("");
    setTag("");
    setActiveId(notes.find((n) => n.deck === id)?.id || "");
    setOpen(true);
    setPeeking(true);
    setMobileList(true);
  };
  const addNote = () => {
    setOpen(true);
    setPeeking(false);
    setManager(false);
    const note: Note = {
      id: crypto.randomUUID(),
      title: "Untitled note",
      deck,
      content: "<p></p>",
      tags: [],
      updated: new Date().toISOString(),
      font: defaultFont,
      completedTasksAtBottom,
    };
    setNotes((n) => [note, ...n]);
    setActiveId(note.id);
    setQuery("");
    setTag("");
    setMobileList(false);
    setTimeout(() => {
      titleInput.current?.focus({ preventScroll: true });
      titleInput.current?.select();
    }, 50);
  };
  const duplicate = () => {
    if (!active) return;
    const note = {
      ...active,
      id: crypto.randomUUID(),
      title: `${active.title} (copy)`,
      updated: new Date().toISOString(),
    };
    setNotes((n) => [note, ...n]);
    setActiveId(note.id);
    showToast("Note duplicated");
  };
  useEffect(() => {
    const handleNew = (e: KeyboardEvent) => {
      if (
        ((e.metaKey || e.ctrlKey) && !e.altKey && e.code === "KeyN") ||
        (e.ctrlKey && e.altKey && !e.metaKey && e.code === "KeyN")
      ) {
        e.preventDefault();
        setOpen(true);
        addNote();
      }
    };
    window.addEventListener("keydown", handleNew);
    return () => window.removeEventListener("keydown", handleNew);
  }, [deck]);
  const removeNote = (note: Note) => {
    setDeleted({
      note,
      index: notes.findIndex((n) => n.id === note.id),
      expires: Date.now() + 10000,
    });
    const next = notes.filter((n) => n.id !== note.id);
    setNotes(next);
    if (activeId === note.id) setActiveId(next.find((n) => n.deck === deck)?.id || "");
  };
  const remove = () => {
    if (active) removeNote(active);
  };
  const undoDelete = () => {
    if (!deleted || Date.now() > deleted.expires) {
      setDeleted(null);
      return;
    }
    setNotes((items) => {
      const next = [...items];
      next.splice(deleted.index, 0, deleted.note);
      return next;
    });
    setActiveId(deleted.note.id);
    setDeck(deleted.note.deck);
    setDeleted(null);
  };
  const openNote = (note: Note) => {
    setActiveId(note.id);
    setDeck(note.deck);
    setOpen(true);
    setPeeking(false);
    setMobileList(false);
    setManager(false);
  };
  const exportNotes = (all: boolean, format: "md" | "txt") => {
    const selection = all ? notes : active ? [active] : [];
    if (!selection.length) return;
    const content = selection
      .map(
        (n) =>
          `${format === "md" ? "# " : ""}${n.title}\n\n${format === "md" ? toMarkdown(n.content) : plainText(n.content)}${n.tags.length ? `\n\n${n.tags.map((t) => "#" + t).join(" ")}` : ""}`,
      )
      .join("\n\n---\n\n");
    const url = URL.createObjectURL(new Blob([content], { type: "text/plain;charset=utf-8" }));
    const a = document.createElement("a");
    a.href = url;
    a.download = `${all ? "EdgeNotes-all-notes" : (active?.title || "note").replace(/[^a-z0-9 -]/gi, "")}.${format}`;
    a.click();
    URL.revokeObjectURL(url);
    showToast(
      `${all ? "All notes" : "Note"} exported as ${format === "md" ? "Markdown" : "plain text"}`,
    );
  };
  return (
    <main
      className={`desktop ${dark ? "dark" : ""} dock-${side} ${dragging ? "dock-dragging" : ""} ${open ? "drawer-open" : "drawer-closed"} ${peeking ? "state-peek" : "state-edit"}`}
      data-deck-state={!open ? "collapsed" : peeking ? "peek" : "editing"}
    >
      <img
        className="desktop-wallpaper"
        src={wallpaper}
        alt="Stars over a quiet alpine lake at night"
        width={1920}
        height={1088}
      />
      <div className="desktop-shade" />
      <div className="desktop-brand">
        <Layers2 size={24} />
        <strong>EdgeNotes</strong>
        <span>A place for the passing thought.</span>
      </div>
      <Button className="manager-launch" variant="ghost" onClick={() => setManager(true)}>
        <Grid2X2 size={15} />
        All notes<span>{notes.length}</span>
      </Button>
      <nav
        ref={dock}
        className="edge-dock"
        aria-label="Deck dock"
        style={dockCoordinates({ side, position: dockPosition }, viewport, dockSize)}
      >
        <Button
          variant="ghost"
          size="icon"
          className="dock-grip"
          aria-label="Move dock"
          title="Drag dock to an edge or corner"
          onPointerDown={(e) => {
            e.preventDefault();
            e.currentTarget.setPointerCapture(e.pointerId);
            setDragging(true);
          }}
          onPointerMove={(e) => {
            if (dragging) moveDock(e.clientX, e.clientY);
          }}
          onPointerUp={(e) => {
            if (dragging) moveDock(e.clientX, e.clientY);
            setDragging(false);
          }}
          onPointerCancel={() => setDragging(false)}
          onKeyDown={(e) => {
            const edges: Record<string, DockSide> = {
              ArrowLeft: "left",
              ArrowRight: "right",
              ArrowUp: "top",
              ArrowDown: "bottom",
            };
            const next = edges[e.key];
            if (next) {
              e.preventDefault();
              chooseSide(next);
            }
          }}
        >
          <Grip size={14} />
        </Button>
        <Button
          variant="ghost"
          size="icon"
          className="dock-logo"
          title="Toggle EdgeNotes"
          aria-label="Toggle drawer"
          onClick={() => setOpen((o) => !o)}
        >
          <Layers2 />
        </Button>
        <div className="dock-decks">
          {decks.map((d, i) => {
            const Icon = deckIcons[i] || FileText;
            return (
              <Button
                variant="ghost"
                key={d.id}
                className={`dock-deck deck-${d.color} ${deck === d.id ? "selected" : ""}`}
                title={d.name}
                aria-label={`${d.name} deck`}
                aria-pressed={deck === d.id && open}
                onClick={() => switchDeck(d.id)}
              >
                <Icon />
                <span>{d.name}</span>
              </Button>
            );
          })}
        </div>
        <div className="dock-bottom">
          <Button
            variant="ghost"
            size="icon"
            className="dock-add-btn"
            title="New note"
            aria-label="New note from dock"
            onClick={addNote}
          >
            <Plus size={16} />
          </Button>
          <Button
            variant="ghost"
            size="icon"
            title={dark ? "Light mode" : "Dark mode"}
            aria-label="Toggle theme"
            onClick={() => setDark((d) => !d)}
          >
            {dark ? <Sun /> : <Moon />}
          </Button>
          <Button
            variant="ghost"
            size="icon"
            className="dock-settings-btn"
            title="Settings & customization"
            aria-label="Settings & customization"
            onClick={() => setSettingsOpen(true)}
          >
            <Settings2 />
          </Button>
        </div>
      </nav>
      <div ref={peek} className="peek-deck" inert={!open || !peeking ? true : undefined}>
        <header>
          <div>
            <span className={`deck-dot deck-${currentDeck.color}`} />
            <strong>{currentDeck.name}</strong>
            <span>{notes.filter((n) => n.deck === deck).length} thoughts</span>
          </div>
          <Button
            variant="ghost"
            size="icon"
            aria-label="Collapse deck"
            title="Collapse deck"
            onClick={() => setOpen(false)}
          >
            <ChevronsRight />
          </Button>
        </header>
        <div className="peek-sheets">
          {notes
            .filter((n) => n.deck === deck)
            .map((n) => (
              <Button
                key={n.id}
                variant="ghost"
                className={`peek-sheet paper-${noteColor(n)}`}
                onClick={() => openNote(n)}
              >
                <div className="peek-sheet-heading">
                  <strong>{n.title}</strong>
                  <ArrowUpRight size={15} />
                </div>
                <p className={getNoteFontClass(n.font || defaultFont)}>
                  {plainText(n.content).slice(0, 260)}
                </p>
                <footer>
                  {n.tags.map((t) => (
                    <span key={t}>#{t}</span>
                  ))}
                </footer>
              </Button>
            ))}
          {!notes.some((n) => n.deck === deck) && (
            <Button variant="ghost" className="peek-sheet paper-paper" onClick={addNote}>
              <Plus />
              <strong>A fresh page</strong>
            </Button>
          )}
        </div>
        <footer>
          <Button variant="ghost" onClick={addNote}>
            <Plus size={14} />
            New note
          </Button>
          <Button variant="ghost" onClick={() => setManager(true)}>
            <Grid2X2 size={14} />
            All notes
          </Button>
        </footer>
      </div>
      <div
        ref={drawer}
        className={`notes-window ${mobileList ? "show-list" : ""}`}
        inert={!open || peeking ? true : undefined}
      >
        <header className="window-header">
          <div className="brand">
            <Layers2 size={20} />
            <span>EdgeNotes</span>
          </div>
          <Button
            variant="ghost"
            size="icon"
            title="Back to deck"
            aria-label="Back to deck"
            onClick={() => setPeeking(true)}
          >
            <Layers2 size={16} />
          </Button>
          <div className="window-actions">
            <Button
              variant="ghost"
              size="icon"
              className={pinned ? "pin-active" : ""}
              title={pinned ? "Unpin drawer" : "Pin drawer"}
              aria-label={pinned ? "Unpin drawer" : "Pin drawer"}
              onClick={() => setPinned((p) => !p)}
            >
              {pinned ? <Pin /> : <PinOff />}
            </Button>
            <span className="window-divider" />
            <Button
              variant="ghost"
              size="icon"
              title="Minimize"
              aria-label="Minimize drawer"
              onClick={() => setOpen(false)}
            >
              <Minus />
            </Button>
            <Button
              variant="ghost"
              size="icon"
              title="Close drawer"
              aria-label="Close drawer"
              onClick={() => setOpen(false)}
            >
              <X />
            </Button>
          </div>
        </header>
        <div className="workspace">
          <aside className="notes-sidebar">
            <div className="search-box">
              <Search size={16} />
              <input
                ref={search}
                aria-label="Search all notes"
                placeholder="Search anything…"
                value={query}
                onChange={(e) => {
                  setQuery(e.target.value);
                  setTag("");
                }}
              />
              <kbd>⌘ K</kbd>
              {query && (
                <Button
                  variant="ghost"
                  size="icon"
                  aria-label="Clear search"
                  onClick={() => setQuery("")}
                >
                  <X />
                </Button>
              )}
            </div>
            <DropdownMenu>
              <DropdownMenuTrigger asChild>
                <Button variant="ghost" className="deck-heading">
                  <span className={`deck-dot deck-${currentDeck.color}`} />
                  <span>{query ? "Search results" : currentDeck.name}</span>
                  <ChevronDown />
                  <span className="note-total">
                    {query ? visible.length : notes.filter((n) => n.deck === deck).length}
                  </span>
                </Button>
              </DropdownMenuTrigger>
              <DropdownMenuContent align="start">
                {decks.map((d, i) => {
                  const Icon = deckIcons[i] || FileText;
                  return (
                    <DropdownMenuItem key={d.id} onClick={() => switchDeck(d.id)}>
                      <Icon className={`deck-${d.color}`} />
                      {d.name}
                    </DropdownMenuItem>
                  );
                })}
              </DropdownMenuContent>
            </DropdownMenu>
            <Button className="new-note" variant="outline" onClick={addNote}>
              <Plus />
              New note<span>⌘ N</span>
            </Button>
            <div className="list-heading">
              <span>{query ? "ACROSS ALL DECKS" : "YOUR NOTES"}</span>
              <DropdownMenu>
                <DropdownMenuTrigger asChild>
                  <Button variant="ghost" size="icon" title="Sort notes" aria-label="Sort notes">
                    <ChevronDown />
                  </Button>
                </DropdownMenuTrigger>
                <DropdownMenuContent>
                  <DropdownMenuItem
                    onClick={() =>
                      setNotes((n) => [...n].sort((a, b) => b.updated.localeCompare(a.updated)))
                    }
                  >
                    Recently edited
                  </DropdownMenuItem>
                  <DropdownMenuItem
                    onClick={() =>
                      setNotes((n) => [...n].sort((a, b) => a.title.localeCompare(b.title)))
                    }
                  >
                    Title A–Z
                  </DropdownMenuItem>
                </DropdownMenuContent>
              </DropdownMenu>
            </div>
            <div className="note-list">
              {visible.map((n) => (
                <Button
                  key={n.id}
                  variant="ghost"
                  className={`note-card ${activeId === n.id ? "active-note" : ""}`}
                  onClick={() => {
                    setActiveId(n.id);
                    setMobileList(false);
                  }}
                >
                  <div className="note-card-title">
                    <FileText size={14} />
                    <strong>{n.title}</strong>
                    {n.id === "website" && <span className="tiny-dot" />}
                  </div>
                  <p>
                    {plainText(n.content).split("\n").filter(Boolean)[0] ||
                      "A fresh page, just for you."}
                  </p>
                  <div className="note-card-meta">
                    <span>
                      {n.updated.slice(0, 10) === "2026-10-13"
                        ? "Today"
                        : new Date(n.updated).toLocaleDateString(undefined, {
                            month: "short",
                            day: "numeric",
                          })}
                    </span>
                    <div>
                      {n.tags.slice(0, 2).map((t) => (
                        <span key={t}>#{t}</span>
                      ))}
                    </div>
                  </div>
                </Button>
              ))}
              {!visible.length && (
                <div className="no-results">
                  <Search />
                  <p>{query || tag ? "No matching notes" : "A fresh deck"}</p>
                  <span>
                    {query || tag
                      ? "Try another search or tag."
                      : "Make room for your first thought."}
                  </span>
                </div>
              )}
            </div>
            <div className="tags-section">
              <div className="list-heading">
                <span>TAGS</span>
                <Hash size={13} />
              </div>
              <div className="tag-filters">
                {allTags.map((t) => (
                  <Button
                    key={t}
                    variant="ghost"
                    className={tag === t ? "tag-selected" : ""}
                    onClick={() => setTag(tag === t ? "" : t)}
                  >
                    #{t}
                  </Button>
                ))}
                {!allTags.length && (
                  <span className="text-xs text-muted-foreground">No tags yet</span>
                )}
              </div>
            </div>
            <footer className="sidebar-footer">
              <span className="local-dot" />
              <span>Just you. Just local.</span>
              <Leaf size={13} />
            </footer>
          </aside>
          <section className={`editor-panel paper-${active ? noteColor(active) : "mint"}`}>
            <div className="editor-topbar">
              <div>
                <Button
                  className="mobile-back"
                  variant="ghost"
                  size="icon"
                  aria-label="Show notes"
                  onClick={() => setMobileList(true)}
                >
                  <ChevronLeft />
                </Button>
                <span className={`deck-dot deck-${activeDeck.color}`} />
                <span>{activeDeck.name}</span>
                <ChevronsRight size={12} />
                <span>{active?.title || "New beginnings"}</span>
              </div>
              <div>
                <DropdownMenu>
                  <DropdownMenuTrigger asChild>
                    <Button
                      variant="ghost"
                      size="icon"
                      title="Export notes"
                      aria-label="Export notes"
                    >
                      <Download />
                    </Button>
                  </DropdownMenuTrigger>
                  <DropdownMenuContent align="end">
                    <DropdownMenuLabel>Export your thoughts</DropdownMenuLabel>
                    <DropdownMenuItem disabled={!active} onClick={() => exportNotes(false, "md")}>
                      Active note · Markdown (.md)
                    </DropdownMenuItem>
                    <DropdownMenuItem disabled={!active} onClick={() => exportNotes(false, "txt")}>
                      Active note · Plain text (.txt)
                    </DropdownMenuItem>
                    <DropdownMenuSeparator />
                    <DropdownMenuItem onClick={() => exportNotes(true, "md")}>
                      All notes · Markdown (.md)
                    </DropdownMenuItem>
                    <DropdownMenuItem onClick={() => exportNotes(true, "txt")}>
                      All notes · Plain text (.txt)
                    </DropdownMenuItem>
                  </DropdownMenuContent>
                </DropdownMenu>
                <DropdownMenu>
                  <DropdownMenuTrigger asChild>
                    <Button
                      variant="ghost"
                      size="icon"
                      title="Note options"
                      aria-label="Note options"
                      disabled={!active}
                    >
                      <MoreHorizontal />
                    </Button>
                  </DropdownMenuTrigger>
                  <DropdownMenuContent align="end">
                    <DropdownMenuItem
                      onClick={() => {
                        titleInput.current?.focus();
                        titleInput.current?.select();
                      }}
                    >
                      <Pencil />
                      Rename note
                    </DropdownMenuItem>
                    <DropdownMenuItem onClick={duplicate}>
                      <Copy />
                      Duplicate note
                    </DropdownMenuItem>
                    <DropdownMenuSeparator />
                    <DropdownMenuLabel>Move to deck</DropdownMenuLabel>
                    {decks
                      .filter((d) => d.id !== active?.deck)
                      .map((d) => (
                        <DropdownMenuItem
                          key={d.id}
                          onClick={() => {
                            update({ deck: d.id });
                            setDeck(d.id);
                            setTag("");
                          }}
                        >
                          <span className={`deck-dot deck-${d.color}`} />
                          {d.name}
                        </DropdownMenuItem>
                      ))}
                    <DropdownMenuSeparator />
                    <DropdownMenuItem className="text-destructive" onClick={remove}>
                      <Trash2 />
                      Delete note
                    </DropdownMenuItem>
                  </DropdownMenuContent>
                </DropdownMenu>
              </div>
            </div>
            {active ? (
              <div className="editor-scroll">
                <div className="note-heading">
                  <input
                    ref={titleInput}
                    className="note-title"
                    aria-label="Note title"
                    value={active.title}
                    onChange={(e) => update({ title: e.target.value })}
                    placeholder="Untitled note"
                  />
                  <div className="note-details">
                    <span>
                      Edited{" "}
                      {new Date(active.updated).toLocaleDateString(undefined, {
                        month: "short",
                        day: "numeric",
                      })}
                    </span>
                    <span className="detail-separator">·</span>
                    {active.tags.map((t) => (
                      <Button
                        key={t}
                        variant="ghost"
                        className="note-tag"
                        title={`Remove #${t}`}
                        onClick={() => update({ tags: active.tags.filter((v) => v !== t) })}
                      >
                        #{t}
                        <X size={10} />
                      </Button>
                    ))}
                    {tagInput ? (
                      <input
                        className="add-tag-input"
                        autoFocus
                        aria-label="New tag"
                        placeholder="tag + Enter"
                        onBlur={() => setTagInput(false)}
                        onKeyDown={(e) => {
                          if (e.key === "Enter") {
                            const value = e.currentTarget.value
                              .replace(/^#/, "")
                              .trim()
                              .toLowerCase()
                              .replace(/\s+/g, "-");
                            if (value && !active.tags.includes(value))
                              update({ tags: [...active.tags, value] });
                            setTagInput(false);
                          }
                          if (e.key === "Escape") setTagInput(false);
                        }}
                      />
                    ) : (
                      <Button variant="ghost" className="add-tag" onClick={() => setTagInput(true)}>
                        <Plus size={11} />
                        Add tag
                      </Button>
                    )}
                  </div>
                </div>
                <NoteEditor
                  key={`${ready}-${active.id}`}
                  note={active}
                  onChange={(content) => update({ content })}
                  onFontChange={(font) => update({ font })}
                  completedTasksAtBottom={completedTasksAtBottom}
                />
                <div className="note-sheet-footer">
                  <div className="color-swatches" aria-label="Note colors">
                    {noteColors.map((c) => (
                      <Button
                        key={c}
                        variant="ghost"
                        size="icon"
                        className={`color-swatch paper-${c} ${noteColor(active) === c ? "swatch-selected" : ""}`}
                        title={`${c} paper`}
                        aria-label={`${c} paper`}
                        aria-pressed={noteColor(active) === c}
                        onClick={() => update({ color: c })}
                      />
                    ))}
                  </div>
                  <Button
                    variant="ghost"
                    size="icon"
                    title="Archive note"
                    aria-label="Archive note"
                    onClick={() => {
                      update({ deck: "archive" });
                      setDeck("archive");
                    }}
                  >
                    <Archive size={15} />
                  </Button>
                  <Button
                    variant="ghost"
                    size="icon"
                    title="Delete note"
                    aria-label="Delete note"
                    onClick={remove}
                  >
                    <Trash2 size={15} />
                  </Button>
                </div>
              </div>
            ) : (
              <div className="empty-editor">
                <FileText size={36} />
                <h2>A little space for something new.</h2>
                <Button variant="outline" onClick={addNote}>
                  <Plus />
                  Create a note
                </Button>
              </div>
            )}
            <footer className={`status-bar ${saveStatus === "Saving…" ? "saving" : ""}`}>
              <div>
                <span>{words} words</span>
                <span className="detail-separator">·</span>
                <span>{text.length} characters</span>
              </div>
              <div>
                <span className="local-dot" />
                <span>{saveStatus}</span>
                <span className="save-time">
                  {active
                    ? new Date(active.updated).toLocaleTimeString(undefined, {
                        hour: "2-digit",
                        minute: "2-digit",
                      })
                    : ""}
                </span>
              </div>
            </footer>
          </section>
        </div>
      </div>
      <div className="desktop-signature">
        <span className="local-dot" />
        Local by nature. Yours by design.
      </div>
      {manager && (
        <NotesManager
          notes={notes}
          onClose={() => setManager(false)}
          onSelect={openNote}
          onNew={addNote}
          onDelete={removeNote}
          onArchive={(n) =>
            setNotes((items) =>
              items.map((item) =>
                item.id === n.id
                  ? {
                      ...item,
                      deck: n.deck === "archive" ? "work" : "archive",
                      updated: new Date().toISOString(),
                    }
                  : item,
              ),
            )
          }
        />
      )}
      <SettingsDialog
        open={settingsOpen}
        onClose={() => setSettingsOpen(false)}
        defaultFont={defaultFont}
        onDefaultFontChange={(font) => {
          setDefaultFont(font);
          if (active) update({ font });
        }}
        dark={dark}
        onDarkToggle={setDark}
        side={side}
        onSideChange={chooseSide}
        completedTasksAtBottom={completedTasksAtBottom}
        onCompletedTasksAtBottomChange={setCompletedTasksAtBottom}
      />
      {deleted && (
        <div className="undo-toast" role="status">
          <div className="undo-progress" />
          <Trash2 size={15} />
          <span>“{deleted.note.title}” deleted · 10s to undo</span>
          <Button variant="ghost" onClick={undoDelete}>
            <Undo2 size={14} />
            Undo
          </Button>
          <Button
            variant="ghost"
            size="icon"
            aria-label="Dismiss undo"
            onClick={() => setDeleted(null)}
          >
            <X size={14} />
          </Button>
        </div>
      )}
      {toast && (
        <div className="toast-message" role="status">
          <Check size={15} />
          {toast}
        </div>
      )}
    </main>
  );
}
