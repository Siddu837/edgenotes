export type Note = {
  id: string;
  deck: string;
  title: string;
  content: string;
  tags: string[];
  updated: string;
  color?: string;
};
export const decks = [
  { id: "work", name: "Work", color: "work", icon: "briefcase" },
  { id: "personal", name: "Personal", color: "personal", icon: "user" },
  { id: "scratchpad", name: "Scratchpad", color: "scratchpad", icon: "file" },
  { id: "ideas", name: "Ideas", color: "ideas", icon: "bulb" },
  { id: "archive", name: "Archive", color: "archive", icon: "archive" },
];
export const STORAGE_KEY = "edgenotes-core-v1";
export const noteColors = ["mint", "sky", "lavender", "yellow", "rose", "peach", "aqua", "paper"];
export function noteColor(note: Note) {
  return note.color && noteColors.includes(note.color) ? note.color : ({ work: "mint", personal: "sky", scratchpad: "yellow", ideas: "rose", archive: "lavender" }[note.deck] || "mint");
}
export const initialNotes: Note[] = [];

const legacySampleIds = new Set(["website", "meeting", "launch", "links", "weekend", "books", "scratch", "idea", "archive"]);
export function removeLegacySamples(notes: Note[]): Note[] {
  return notes.filter(note => !legacySampleIds.has(note.id));
}

export function plainText(html: string) {
  return html
    .replace(/<\/(p|h[1-6]|li|pre|blockquote)>/g, "\n")
    .replace(/<br\s*\/?\s*>/g, "\n")
    .replace(/<[^>]+>/g, "")
    .replace(/&amp;/g, "&")
    .replace(/&nbsp;/g, " ")
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .trim();
}
