import { useState } from "react";
import { Archive, ArrowUpRight, Check, Grid2X2, Layers2, Plus, Search, Trash2, X } from "lucide-react";
import { Button } from "@/components/ui/button";
import { decks, noteColor, plainText, type Note } from "@/lib/notes";

export function NotesManager({ notes, onClose, onSelect, onNew, onArchive, onDelete }: {
  notes: Note[]; onClose: () => void; onSelect: (note: Note) => void; onNew: () => void;
  onArchive: (note: Note) => void; onDelete: (note: Note) => void;
}) {
  const [filter, setFilter] = useState("all");
  const [query, setQuery] = useState("");
  const [selected, setSelected] = useState(notes[0]?.id || "");
  const visible = notes.filter(n => (filter === "all" || (filter === "deck" ? n.deck !== "archive" : n.deck === filter)) && `${n.title} ${plainText(n.content)} ${n.tags.join(" ")}`.toLowerCase().includes(query.toLowerCase()));
  const active = visible.find(n => n.id === selected) || visible[0];
  return <div className="manager-overlay dark" onPointerDown={e => { if (e.target === e.currentTarget) onClose(); }}>
    <section className="notes-manager" role="dialog" aria-modal="true" aria-label="All notes manager" onKeyDown={e => { if (e.key === "Escape") { e.stopPropagation(); onClose(); } }}>
      <header className="manager-header"><Layers2 size={19}/><strong>EdgeNotes</strong><span>All your thoughts, together.</span><Button variant="ghost" size="icon" aria-label="Close manager" onClick={onClose}><X/></Button></header>
      <div className="manager-layout">
        <nav className="manager-nav" aria-label="Manager filters">
          <Button onClick={onNew}><Plus/>New note</Button>
          <span className="manager-label">LIBRARY</span>
          {([{id:"all",name:"All notes",Icon:Grid2X2},{id:"deck",name:"On the deck",Icon:Layers2},{id:"archive",name:"Archive",Icon:Archive}]).map(({id,name,Icon}) => <Button key={id} variant="ghost" className={filter === id ? "manager-selected" : ""} onClick={()=>setFilter(id)}><Icon/>{name}<span>{notes.filter(n => id === "all" || (id === "deck" ? n.deck !== "archive" : n.deck === "archive")).length}</span></Button>)}
          <span className="manager-label">DECKS</span>
          {decks.filter(d=>d.id!=="archive").map(d=><Button key={d.id} variant="ghost" className={filter===d.id ? "manager-selected" : ""} onClick={()=>setFilter(d.id)}><i className={`deck-dot deck-${d.color}`}/>{d.name}<span>{notes.filter(n=>n.deck===d.id).length}</span></Button>)}
          <footer><Check size={13}/>Saved on this device</footer>
        </nav>
        <div className="manager-list"><div className="manager-list-heading"><h2>{filter === "all" ? "All notes" : filter === "deck" ? "On the deck" : decks.find(d=>d.id===filter)?.name}</h2><span>{visible.length} notes</span></div><label className="search-box"><Search size={15}/><input aria-label="Search manager notes" placeholder="Find a thought…" value={query} onChange={e=>setQuery(e.target.value)}/></label>
          <div className="manager-rows">{visible.map(n=><Button variant="ghost" key={n.id} className={`manager-row ${active?.id===n.id?"manager-selected":""}`} onClick={()=>setSelected(n.id)}><i className={`note-stripe paper-${noteColor(n)}`}/><div><strong>{n.title}</strong><p>{plainText(n.content).split("\n")[0]||"A blank page"}</p></div><span>{new Date(n.updated).toLocaleDateString(undefined,{month:"short",day:"numeric"})}</span></Button>)}{!visible.length&&<div className="no-results"><Search/><p>No matching notes</p></div>}</div>
        </div>
        <div className="manager-preview">{active ? <><header><span><i className={`deck-dot deck-${active.deck}`}/>{active.deck === "archive" ? "ARCHIVED" : "ON THE DECK"}</span><div><Button variant="ghost" size="icon" title="Open note" aria-label="Open selected note" onClick={()=>onSelect(active)}><ArrowUpRight/></Button><Button variant="ghost" size="icon" title={active.deck === "archive" ? "Restore note" : "Archive note"} aria-label={active.deck === "archive" ? "Restore selected note" : "Archive selected note"} onClick={()=>onArchive(active)}><Archive/></Button><Button variant="ghost" size="icon" title="Delete note" aria-label="Delete selected note" onClick={()=>onDelete(active)}><Trash2/></Button></div></header><div className={`manager-sheet paper-${noteColor(active)}`}><h2>{active.title}</h2><div className="handwritten manager-body">{plainText(active.content)}</div><footer>{active.tags.map(t=><span key={t}>#{t}</span>)}<Button variant="ghost" size="sm" onClick={()=>onSelect(active)}>Open note<ArrowUpRight size={14}/></Button></footer></div></> : <div className="empty-editor"><Layers2/><h2>A little room for something new.</h2></div>}</div>
      </div>
    </section>
  </div>;
}